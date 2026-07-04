#include "battlesnake_json.h"

#include "../datatypes/coord.h"
#include "../datatypes/snake.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define BS_SKIP_MAX_DEPTH 128

typedef struct {
    const char* cur;
    const char* end;
    BsArena* arena;
} BsJsonParser;

typedef struct BsCoordNode {
    Coord coord;
    struct BsCoordNode* next;
} BsCoordNode;

typedef struct BsSnakeNode {
    char* id;
    char* name;
    int health;
    Coord* body;
    int body_len;
    int length;
    struct BsSnakeNode* next;
} BsSnakeNode;

typedef struct {
    int width;
    int height;
    bool has_width;
    bool has_height;
    bool has_snakes;
    BsCoordNode* food_head;
    BsCoordNode* food_tail;
    BsCoordNode* hazards_head;
    BsCoordNode* hazards_tail;
    BsSnakeNode* snakes_head;
    BsSnakeNode* snakes_tail;
} BsParsedBoard;

typedef struct {
    char* game_id;
    bool has_game_id;
    int turn;
    int timeout_ms;
    char* you_id;
    bool has_you_id;
    char* ruleset_name;
    int hazard_damage;
    BsParsedBoard board;
} BsParsedRequest;

static void bs_skip_ws(BsJsonParser* parser) {
    while (parser->cur < parser->end) {
        char c = *parser->cur;
        if (c != ' ' && c != '\n' && c != '\r' && c != '\t') {
            break;
        }
        parser->cur++;
    }
}

static bool bs_consume(BsJsonParser* parser, char expected) {
    bs_skip_ws(parser);
    if (parser->cur >= parser->end || *parser->cur != expected) {
        return false;
    }
    parser->cur++;
    return true;
}

static int bs_hex_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    return -1;
}

static bool bs_parse_hex4(const char* input, uint32_t* out_value) {
    uint32_t value = 0;
    for (int i = 0; i < 4; i++) {
        int digit = bs_hex_value(input[i]);
        if (digit < 0) {
            return false;
        }
        value = (value << 4) | (uint32_t)digit;
    }
    *out_value = value;
    return true;
}

static size_t bs_utf8_len(uint32_t codepoint) {
    if (codepoint <= 0x7fu) {
        return 1;
    }
    if (codepoint <= 0x7ffu) {
        return 2;
    }
    if (codepoint <= 0xffffu) {
        return 3;
    }
    if (codepoint <= 0x10ffffu) {
        return 4;
    }
    return 0;
}

static char* bs_utf8_write(char* out, uint32_t codepoint) {
    if (codepoint <= 0x7fu) {
        *out++ = (char)codepoint;
        return out;
    }
    if (codepoint <= 0x7ffu) {
        *out++ = (char)(0xc0u | (codepoint >> 6));
        *out++ = (char)(0x80u | (codepoint & 0x3fu));
        return out;
    }
    if (codepoint <= 0xffffu) {
        *out++ = (char)(0xe0u | (codepoint >> 12));
        *out++ = (char)(0x80u | ((codepoint >> 6) & 0x3fu));
        *out++ = (char)(0x80u | (codepoint & 0x3fu));
        return out;
    }
    *out++ = (char)(0xf0u | (codepoint >> 18));
    *out++ = (char)(0x80u | ((codepoint >> 12) & 0x3fu));
    *out++ = (char)(0x80u | ((codepoint >> 6) & 0x3fu));
    *out++ = (char)(0x80u | (codepoint & 0x3fu));
    return out;
}

static BsJsonStatus bs_measure_string(
    const char* start,
    const char* end,
    const char** out_after,
    size_t* out_decoded_len
) {
    if (start >= end || *start != '"') {
        return BS_JSON_MALFORMED;
    }

    const char* scan = start + 1;
    size_t decoded_len = 0;

    while (scan < end) {
        unsigned char c = (unsigned char)*scan++;
        if (c == '"') {
            *out_after = scan;
            *out_decoded_len = decoded_len;
            return BS_JSON_OK;
        }
        if (c < 0x20u) {
            return BS_JSON_MALFORMED;
        }
        if (c != '\\') {
            decoded_len++;
            continue;
        }

        if (scan >= end) {
            return BS_JSON_MALFORMED;
        }

        char escape = *scan++;
        switch (escape) {
            case '"':
            case '\\':
            case '/':
            case 'b':
            case 'f':
            case 'n':
            case 'r':
            case 't':
                decoded_len++;
                break;
            case 'u': {
                if ((size_t)(end - scan) < 4u) {
                    return BS_JSON_MALFORMED;
                }
                uint32_t codepoint = 0;
                if (!bs_parse_hex4(scan, &codepoint)) {
                    return BS_JSON_MALFORMED;
                }
                scan += 4;
                if (codepoint >= 0xd800u && codepoint <= 0xdbffu) {
                    if ((size_t)(end - scan) < 6u || scan[0] != '\\' || scan[1] != 'u') {
                        return BS_JSON_MALFORMED;
                    }
                    uint32_t low = 0;
                    if (!bs_parse_hex4(scan + 2, &low) || low < 0xdc00u || low > 0xdfffu) {
                        return BS_JSON_MALFORMED;
                    }
                    scan += 6;
                    codepoint = 0x10000u + (((codepoint - 0xd800u) << 10) | (low - 0xdc00u));
                } else if (codepoint >= 0xdc00u && codepoint <= 0xdfffu) {
                    return BS_JSON_MALFORMED;
                }
                decoded_len += bs_utf8_len(codepoint);
                break;
            }
            default:
                return BS_JSON_MALFORMED;
        }
    }

    return BS_JSON_MALFORMED;
}

static BsJsonStatus bs_parse_string(BsJsonParser* parser, char** out_string) {
    bs_skip_ws(parser);

    const char* after = NULL;
    size_t decoded_len = 0;
    BsJsonStatus status = bs_measure_string(parser->cur, parser->end, &after, &decoded_len);
    if (status != BS_JSON_OK) {
        return status;
    }

    char* value = (char*)BsArenaAlloc(parser->arena, decoded_len + 1);
    if (value == NULL) {
        return BS_JSON_NO_MEMORY;
    }

    const char* scan = parser->cur + 1;
    const char* closing_quote = after - 1;
    char* write = value;
    while (scan < closing_quote) {
        char c = *scan++;
        if (c != '\\') {
            *write++ = c;
            continue;
        }

        char escape = *scan++;
        switch (escape) {
            case '"':
            case '\\':
            case '/':
                *write++ = escape;
                break;
            case 'b':
                *write++ = '\b';
                break;
            case 'f':
                *write++ = '\f';
                break;
            case 'n':
                *write++ = '\n';
                break;
            case 'r':
                *write++ = '\r';
                break;
            case 't':
                *write++ = '\t';
                break;
            case 'u': {
                uint32_t codepoint = 0;
                (void)bs_parse_hex4(scan, &codepoint);
                scan += 4;
                if (codepoint >= 0xd800u && codepoint <= 0xdbffu) {
                    uint32_t low = 0;
                    (void)bs_parse_hex4(scan + 2, &low);
                    scan += 6;
                    codepoint = 0x10000u + (((codepoint - 0xd800u) << 10) | (low - 0xdc00u));
                }
                write = bs_utf8_write(write, codepoint);
                break;
            }
            default:
                return BS_JSON_MALFORMED;
        }
    }

    *write = '\0';
    parser->cur = after;
    *out_string = value;
    return BS_JSON_OK;
}

static BsJsonStatus bs_skip_string(BsJsonParser* parser) {
    bs_skip_ws(parser);

    const char* after = NULL;
    size_t decoded_len = 0;
    BsJsonStatus status = bs_measure_string(parser->cur, parser->end, &after, &decoded_len);
    if (status != BS_JSON_OK) {
        return status;
    }

    parser->cur = after;
    return BS_JSON_OK;
}

static BsJsonStatus bs_parse_integer(BsJsonParser* parser, int* out_value) {
    bs_skip_ws(parser);
    if (parser->cur >= parser->end) {
        return BS_JSON_MALFORMED;
    }

    const char* start = parser->cur;
    bool negative = false;
    if (*parser->cur == '-') {
        negative = true;
        parser->cur++;
    }

    if (parser->cur >= parser->end || *parser->cur < '0' || *parser->cur > '9') {
        parser->cur = start;
        return BS_JSON_MALFORMED;
    }

    if (*parser->cur == '0' && parser->cur + 1 < parser->end) {
        char next = parser->cur[1];
        if (next >= '0' && next <= '9') {
            parser->cur = start;
            return BS_JSON_MALFORMED;
        }
    }

    int64_t limit = negative ? -(int64_t)INT_MIN : INT_MAX;
    int64_t value = 0;
    while (parser->cur < parser->end && *parser->cur >= '0' && *parser->cur <= '9') {
        int digit = *parser->cur - '0';
        if (value > (limit - digit) / 10) {
            parser->cur = start;
            return BS_JSON_MALFORMED;
        }
        value = value * 10 + digit;
        parser->cur++;
    }

    if (parser->cur < parser->end) {
        char tail = *parser->cur;
        if (tail == '.' || tail == 'e' || tail == 'E') {
            parser->cur = start;
            return BS_JSON_MALFORMED;
        }
    }

    if (negative) {
        value = -value;
    }
    *out_value = (int)value;
    return BS_JSON_OK;
}

static BsJsonStatus bs_skip_number(BsJsonParser* parser) {
    bs_skip_ws(parser);
    if (parser->cur >= parser->end) {
        return BS_JSON_MALFORMED;
    }

    const char* start = parser->cur;
    if (*parser->cur == '-') {
        parser->cur++;
    }
    if (parser->cur >= parser->end || *parser->cur < '0' || *parser->cur > '9') {
        parser->cur = start;
        return BS_JSON_MALFORMED;
    }
    if (*parser->cur == '0') {
        parser->cur++;
    } else {
        while (parser->cur < parser->end && *parser->cur >= '0' && *parser->cur <= '9') {
            parser->cur++;
        }
    }
    if (parser->cur < parser->end && *parser->cur == '.') {
        parser->cur++;
        if (parser->cur >= parser->end || *parser->cur < '0' || *parser->cur > '9') {
            parser->cur = start;
            return BS_JSON_MALFORMED;
        }
        while (parser->cur < parser->end && *parser->cur >= '0' && *parser->cur <= '9') {
            parser->cur++;
        }
    }
    if (parser->cur < parser->end && (*parser->cur == 'e' || *parser->cur == 'E')) {
        parser->cur++;
        if (parser->cur < parser->end && (*parser->cur == '+' || *parser->cur == '-')) {
            parser->cur++;
        }
        if (parser->cur >= parser->end || *parser->cur < '0' || *parser->cur > '9') {
            parser->cur = start;
            return BS_JSON_MALFORMED;
        }
        while (parser->cur < parser->end && *parser->cur >= '0' && *parser->cur <= '9') {
            parser->cur++;
        }
    }
    return BS_JSON_OK;
}

static BsJsonStatus bs_skip_literal(BsJsonParser* parser, const char* literal) {
    bs_skip_ws(parser);
    size_t len = strlen(literal);
    if ((size_t)(parser->end - parser->cur) < len || memcmp(parser->cur, literal, len) != 0) {
        return BS_JSON_MALFORMED;
    }
    parser->cur += len;
    return BS_JSON_OK;
}

static BsJsonStatus bs_skip_value(BsJsonParser* parser, int depth);

static BsJsonStatus bs_skip_array(BsJsonParser* parser, int depth) {
    if (depth >= BS_SKIP_MAX_DEPTH) {
        return BS_JSON_MALFORMED;
    }
    if (!bs_consume(parser, '[')) {
        return BS_JSON_MALFORMED;
    }
    bs_skip_ws(parser);
    if (bs_consume(parser, ']')) {
        return BS_JSON_OK;
    }
    while (true) {
        BsJsonStatus status = bs_skip_value(parser, depth + 1);
        if (status != BS_JSON_OK) {
            return status;
        }
        bs_skip_ws(parser);
        if (bs_consume(parser, ']')) {
            return BS_JSON_OK;
        }
        if (!bs_consume(parser, ',')) {
            return BS_JSON_MALFORMED;
        }
    }
}

static BsJsonStatus bs_skip_object(BsJsonParser* parser, int depth) {
    if (depth >= BS_SKIP_MAX_DEPTH) {
        return BS_JSON_MALFORMED;
    }
    if (!bs_consume(parser, '{')) {
        return BS_JSON_MALFORMED;
    }
    bs_skip_ws(parser);
    if (bs_consume(parser, '}')) {
        return BS_JSON_OK;
    }
    while (true) {
        BsJsonStatus status = bs_skip_string(parser);
        if (status != BS_JSON_OK) {
            return status;
        }
        if (!bs_consume(parser, ':')) {
            return BS_JSON_MALFORMED;
        }
        status = bs_skip_value(parser, depth + 1);
        if (status != BS_JSON_OK) {
            return status;
        }
        bs_skip_ws(parser);
        if (bs_consume(parser, '}')) {
            return BS_JSON_OK;
        }
        if (!bs_consume(parser, ',')) {
            return BS_JSON_MALFORMED;
        }
    }
}

static BsJsonStatus bs_skip_value(BsJsonParser* parser, int depth) {
    bs_skip_ws(parser);
    if (parser->cur >= parser->end) {
        return BS_JSON_MALFORMED;
    }
    switch (*parser->cur) {
        case '{':
            return bs_skip_object(parser, depth);
        case '[':
            return bs_skip_array(parser, depth);
        case '"':
            return bs_skip_string(parser);
        case 't':
            return bs_skip_literal(parser, "true");
        case 'f':
            return bs_skip_literal(parser, "false");
        case 'n':
            return bs_skip_literal(parser, "null");
        default:
            return bs_skip_number(parser);
    }
}

static BsJsonStatus bs_parse_nullable_string(
    BsJsonParser* parser,
    char** out_value,
    bool* was_null
) {
    bs_skip_ws(parser);
    if (parser->cur < parser->end && *parser->cur == 'n') {
        BsJsonStatus status = bs_skip_literal(parser, "null");
        if (status != BS_JSON_OK) {
            return status;
        }
        *was_null = true;
        *out_value = NULL;
        return BS_JSON_OK;
    }
    *was_null = false;
    return bs_parse_string(parser, out_value);
}

static BsJsonStatus bs_parse_nullable_integer(
    BsJsonParser* parser,
    int* out_value,
    bool* was_null
) {
    bs_skip_ws(parser);
    if (parser->cur < parser->end && *parser->cur == 'n') {
        BsJsonStatus status = bs_skip_literal(parser, "null");
        if (status != BS_JSON_OK) {
            return status;
        }
        *was_null = true;
        return BS_JSON_OK;
    }
    *was_null = false;
    return bs_parse_integer(parser, out_value);
}

static BsJsonStatus bs_append_coord_node(
    BsArena* arena,
    BsCoordNode** head,
    BsCoordNode** tail,
    Coord coord
) {
    BsCoordNode* node = (BsCoordNode*)BsArenaAlloc(arena, sizeof(BsCoordNode));
    if (node == NULL) {
        return BS_JSON_NO_MEMORY;
    }
    node->coord = coord;
    node->next = NULL;
    if (*tail != NULL) {
        (*tail)->next = node;
    } else {
        *head = node;
    }
    *tail = node;
    return BS_JSON_OK;
}

static BsJsonStatus bs_parse_coord(BsJsonParser* parser, Coord* out_coord) {
    if (!bs_consume(parser, '{')) {
        return BS_JSON_MALFORMED;
    }

    bool has_x = false;
    bool has_y = false;
    int x = 0;
    int y = 0;

    bs_skip_ws(parser);
    if (bs_consume(parser, '}')) {
        return BS_JSON_MALFORMED;
    }

    while (true) {
        char* key = NULL;
        BsJsonStatus status = bs_parse_string(parser, &key);
        if (status != BS_JSON_OK) {
            return status;
        }
        if (!bs_consume(parser, ':')) {
            return BS_JSON_MALFORMED;
        }
        if (strcmp(key, "x") == 0) {
            status = bs_parse_integer(parser, &x);
            if (status != BS_JSON_OK) {
                return status;
            }
            has_x = true;
        } else if (strcmp(key, "y") == 0) {
            status = bs_parse_integer(parser, &y);
            if (status != BS_JSON_OK) {
                return status;
            }
            has_y = true;
        } else {
            status = bs_skip_value(parser, 0);
            if (status != BS_JSON_OK) {
                return status;
            }
        }

        bs_skip_ws(parser);
        if (bs_consume(parser, '}')) {
            break;
        }
        if (!bs_consume(parser, ',')) {
            return BS_JSON_MALFORMED;
        }
    }

    if (!has_x || !has_y) {
        return BS_JSON_MALFORMED;
    }
    out_coord->x = x;
    out_coord->y = y;
    return BS_JSON_OK;
}

static BsJsonStatus bs_parse_coord_array(
    BsJsonParser* parser,
    BsCoordNode** head,
    BsCoordNode** tail
) {
    if (!bs_consume(parser, '[')) {
        return BS_JSON_MALFORMED;
    }
    bs_skip_ws(parser);
    if (bs_consume(parser, ']')) {
        return BS_JSON_OK;
    }

    while (true) {
        Coord coord = {0, 0};
        BsJsonStatus status = bs_parse_coord(parser, &coord);
        if (status != BS_JSON_OK) {
            return status;
        }
        status = bs_append_coord_node(parser->arena, head, tail, coord);
        if (status != BS_JSON_OK) {
            return status;
        }

        bs_skip_ws(parser);
        if (bs_consume(parser, ']')) {
            return BS_JSON_OK;
        }
        if (!bs_consume(parser, ',')) {
            return BS_JSON_MALFORMED;
        }
    }
}

static BsJsonStatus bs_parse_snake(BsJsonParser* parser, BsSnakeNode** out_snake) {
    if (!bs_consume(parser, '{')) {
        return BS_JSON_MALFORMED;
    }

    char* id = NULL;
    bool has_id = false;
    char* name = NULL;
    int health = 100;
    int length = 0;
    bool has_length = false;
    bool has_body = false;
    BsCoordNode* body_head = NULL;
    BsCoordNode* body_tail = NULL;
    int body_len = 0;

    bs_skip_ws(parser);
    if (bs_consume(parser, '}')) {
        return BS_JSON_MISSING_REQUIRED;
    }

    while (true) {
        char* key = NULL;
        BsJsonStatus status = bs_parse_string(parser, &key);
        if (status != BS_JSON_OK) {
            return status;
        }
        if (!bs_consume(parser, ':')) {
            return BS_JSON_MALFORMED;
        }

        if (strcmp(key, "id") == 0) {
            bool is_null = false;
            status = bs_parse_nullable_string(parser, &id, &is_null);
            if (status != BS_JSON_OK) {
                return status;
            }
            if (!is_null) {
                has_id = true;
            }
        } else if (strcmp(key, "name") == 0) {
            bool is_null = false;
            status = bs_parse_nullable_string(parser, &name, &is_null);
            if (status != BS_JSON_OK) {
                return status;
            }
            if (is_null) {
                name = NULL;
            }
        } else if (strcmp(key, "health") == 0) {
            bool is_null = false;
            status = bs_parse_nullable_integer(parser, &health, &is_null);
            if (status != BS_JSON_OK) {
                return status;
            }
            if (is_null) {
                health = 100;
            } else if (health < 0) {
                return BS_JSON_MALFORMED;
            }
        } else if (strcmp(key, "length") == 0) {
            bool is_null = false;
            status = bs_parse_nullable_integer(parser, &length, &is_null);
            if (status != BS_JSON_OK) {
                return status;
            }
            has_length = !is_null;
            if (has_length && length < 0) {
                return BS_JSON_MALFORMED;
            }
        } else if (strcmp(key, "body") == 0) {
            status = bs_parse_coord_array(parser, &body_head, &body_tail);
            if (status != BS_JSON_OK) {
                return status;
            }
            has_body = true;
        } else {
            status = bs_skip_value(parser, 0);
            if (status != BS_JSON_OK) {
                return status;
            }
        }

        bs_skip_ws(parser);
        if (bs_consume(parser, '}')) {
            break;
        }
        if (!bs_consume(parser, ',')) {
            return BS_JSON_MALFORMED;
        }
    }

    if (!has_id || !has_body) {
        return BS_JSON_MISSING_REQUIRED;
    }

    if (name == NULL) {
        name = (char*)BsArenaStrDup(parser->arena, "", 0);
        if (name == NULL) {
            return BS_JSON_NO_MEMORY;
        }
    }

    Coord* body = NULL;
    if (body_head != NULL) {
        int count = 0;
        for (BsCoordNode* node = body_head; node != NULL; node = node->next) {
            count++;
        }
        body_len = count;
        body = (Coord*)BsArenaAlloc(parser->arena, (size_t)body_len * sizeof(Coord));
        if (body == NULL) {
            return BS_JSON_NO_MEMORY;
        }
        int index = 0;
        for (BsCoordNode* node = body_head; node != NULL; node = node->next) {
            body[index++] = node->coord;
        }
    }

    BsSnakeNode* snake = (BsSnakeNode*)BsArenaAlloc(parser->arena, sizeof(BsSnakeNode));
    if (snake == NULL) {
        return BS_JSON_NO_MEMORY;
    }
    snake->id = id;
    snake->name = name;
    snake->health = health;
    snake->body = body;
    snake->body_len = body_len;
    snake->length = has_length ? length : body_len;
    snake->next = NULL;

    *out_snake = snake;
    return BS_JSON_OK;
}

static BsJsonStatus bs_parse_snakes_array(BsJsonParser* parser, BsParsedBoard* board) {
    if (!bs_consume(parser, '[')) {
        return BS_JSON_MALFORMED;
    }
    board->has_snakes = true;
    bs_skip_ws(parser);
    if (bs_consume(parser, ']')) {
        return BS_JSON_OK;
    }

    while (true) {
        BsSnakeNode* snake = NULL;
        BsJsonStatus status = bs_parse_snake(parser, &snake);
        if (status != BS_JSON_OK) {
            return status;
        }
        if (board->snakes_tail != NULL) {
            board->snakes_tail->next = snake;
        } else {
            board->snakes_head = snake;
        }
        board->snakes_tail = snake;

        bs_skip_ws(parser);
        if (bs_consume(parser, ']')) {
            return BS_JSON_OK;
        }
        if (!bs_consume(parser, ',')) {
            return BS_JSON_MALFORMED;
        }
    }
}

static BsJsonStatus bs_parse_ruleset_settings(BsJsonParser* parser, BsParsedRequest* request) {
    if (!bs_consume(parser, '{')) {
        return BS_JSON_MALFORMED;
    }
    bs_skip_ws(parser);
    if (bs_consume(parser, '}')) {
        return BS_JSON_OK;
    }

    while (true) {
        char* key = NULL;
        BsJsonStatus status = bs_parse_string(parser, &key);
        if (status != BS_JSON_OK) {
            return status;
        }
        if (!bs_consume(parser, ':')) {
            return BS_JSON_MALFORMED;
        }
        if (strcmp(key, "hazardDamagePerTurn") == 0) {
            bool is_null = false;
            status = bs_parse_nullable_integer(parser, &request->hazard_damage, &is_null);
            if (status != BS_JSON_OK) {
                return status;
            }
            if (is_null) {
                request->hazard_damage = 15;
            } else if (request->hazard_damage < 0) {
                return BS_JSON_MALFORMED;
            }
        } else {
            status = bs_skip_value(parser, 0);
            if (status != BS_JSON_OK) {
                return status;
            }
        }

        bs_skip_ws(parser);
        if (bs_consume(parser, '}')) {
            return BS_JSON_OK;
        }
        if (!bs_consume(parser, ',')) {
            return BS_JSON_MALFORMED;
        }
    }
}

static BsJsonStatus bs_parse_ruleset(BsJsonParser* parser, BsParsedRequest* request) {
    if (!bs_consume(parser, '{')) {
        return BS_JSON_MALFORMED;
    }
    bs_skip_ws(parser);
    if (bs_consume(parser, '}')) {
        return BS_JSON_OK;
    }

    while (true) {
        char* key = NULL;
        BsJsonStatus status = bs_parse_string(parser, &key);
        if (status != BS_JSON_OK) {
            return status;
        }
        if (!bs_consume(parser, ':')) {
            return BS_JSON_MALFORMED;
        }
        if (strcmp(key, "name") == 0) {
            bool is_null = false;
            char* name = NULL;
            status = bs_parse_nullable_string(parser, &name, &is_null);
            if (status != BS_JSON_OK) {
                return status;
            }
            if (!is_null) {
                request->ruleset_name = name;
            }
        } else if (strcmp(key, "settings") == 0) {
            bs_skip_ws(parser);
            if (parser->cur < parser->end && *parser->cur == 'n') {
                status = bs_skip_literal(parser, "null");
            } else {
                status = bs_parse_ruleset_settings(parser, request);
            }
            if (status != BS_JSON_OK) {
                return status;
            }
        } else {
            status = bs_skip_value(parser, 0);
            if (status != BS_JSON_OK) {
                return status;
            }
        }

        bs_skip_ws(parser);
        if (bs_consume(parser, '}')) {
            return BS_JSON_OK;
        }
        if (!bs_consume(parser, ',')) {
            return BS_JSON_MALFORMED;
        }
    }
}

static BsJsonStatus bs_parse_game(BsJsonParser* parser, BsParsedRequest* request) {
    if (!bs_consume(parser, '{')) {
        return BS_JSON_MALFORMED;
    }
    bs_skip_ws(parser);
    if (bs_consume(parser, '}')) {
        return BS_JSON_MISSING_REQUIRED;
    }

    while (true) {
        char* key = NULL;
        BsJsonStatus status = bs_parse_string(parser, &key);
        if (status != BS_JSON_OK) {
            return status;
        }
        if (!bs_consume(parser, ':')) {
            return BS_JSON_MALFORMED;
        }
        if (strcmp(key, "id") == 0) {
            bool is_null = false;
            status = bs_parse_nullable_string(parser, &request->game_id, &is_null);
            if (status != BS_JSON_OK) {
                return status;
            }
            request->has_game_id = !is_null;
        } else if (strcmp(key, "timeout") == 0) {
            bool is_null = false;
            status = bs_parse_nullable_integer(parser, &request->timeout_ms, &is_null);
            if (status != BS_JSON_OK) {
                return status;
            }
            if (is_null) {
                request->timeout_ms = 500;
            } else if (request->timeout_ms < 0) {
                return BS_JSON_MALFORMED;
            }
        } else if (strcmp(key, "ruleset") == 0) {
            bs_skip_ws(parser);
            if (parser->cur < parser->end && *parser->cur == 'n') {
                status = bs_skip_literal(parser, "null");
            } else {
                status = bs_parse_ruleset(parser, request);
            }
            if (status != BS_JSON_OK) {
                return status;
            }
        } else {
            status = bs_skip_value(parser, 0);
            if (status != BS_JSON_OK) {
                return status;
            }
        }

        bs_skip_ws(parser);
        if (bs_consume(parser, '}')) {
            return BS_JSON_OK;
        }
        if (!bs_consume(parser, ',')) {
            return BS_JSON_MALFORMED;
        }
    }
}

static BsJsonStatus bs_parse_board(BsJsonParser* parser, BsParsedBoard* board) {
    if (!bs_consume(parser, '{')) {
        return BS_JSON_MALFORMED;
    }
    bs_skip_ws(parser);
    if (bs_consume(parser, '}')) {
        return BS_JSON_MISSING_REQUIRED;
    }

    while (true) {
        char* key = NULL;
        BsJsonStatus status = bs_parse_string(parser, &key);
        if (status != BS_JSON_OK) {
            return status;
        }
        if (!bs_consume(parser, ':')) {
            return BS_JSON_MALFORMED;
        }
        if (strcmp(key, "width") == 0) {
            status = bs_parse_integer(parser, &board->width);
            if (status != BS_JSON_OK) {
                return status;
            }
            board->has_width = true;
        } else if (strcmp(key, "height") == 0) {
            status = bs_parse_integer(parser, &board->height);
            if (status != BS_JSON_OK) {
                return status;
            }
            board->has_height = true;
        } else if (strcmp(key, "snakes") == 0) {
            status = bs_parse_snakes_array(parser, board);
            if (status != BS_JSON_OK) {
                return status;
            }
        } else if (strcmp(key, "food") == 0) {
            bs_skip_ws(parser);
            if (parser->cur < parser->end && *parser->cur == 'n') {
                status = bs_skip_literal(parser, "null");
            } else {
                status = bs_parse_coord_array(parser, &board->food_head, &board->food_tail);
            }
            if (status != BS_JSON_OK) {
                return status;
            }
        } else if (strcmp(key, "hazards") == 0) {
            bs_skip_ws(parser);
            if (parser->cur < parser->end && *parser->cur == 'n') {
                status = bs_skip_literal(parser, "null");
            } else {
                status = bs_parse_coord_array(parser, &board->hazards_head, &board->hazards_tail);
            }
            if (status != BS_JSON_OK) {
                return status;
            }
        } else {
            status = bs_skip_value(parser, 0);
            if (status != BS_JSON_OK) {
                return status;
            }
        }

        bs_skip_ws(parser);
        if (bs_consume(parser, '}')) {
            return BS_JSON_OK;
        }
        if (!bs_consume(parser, ',')) {
            return BS_JSON_MALFORMED;
        }
    }
}

static BsJsonStatus bs_parse_you(BsJsonParser* parser, BsParsedRequest* request) {
    if (!bs_consume(parser, '{')) {
        return BS_JSON_MALFORMED;
    }
    bs_skip_ws(parser);
    if (bs_consume(parser, '}')) {
        return BS_JSON_MISSING_REQUIRED;
    }

    while (true) {
        char* key = NULL;
        BsJsonStatus status = bs_parse_string(parser, &key);
        if (status != BS_JSON_OK) {
            return status;
        }
        if (!bs_consume(parser, ':')) {
            return BS_JSON_MALFORMED;
        }
        if (strcmp(key, "id") == 0) {
            bool is_null = false;
            status = bs_parse_nullable_string(parser, &request->you_id, &is_null);
            if (status != BS_JSON_OK) {
                return status;
            }
            request->has_you_id = !is_null;
        } else {
            status = bs_skip_value(parser, 0);
            if (status != BS_JSON_OK) {
                return status;
            }
        }

        bs_skip_ws(parser);
        if (bs_consume(parser, '}')) {
            return BS_JSON_OK;
        }
        if (!bs_consume(parser, ',')) {
            return BS_JSON_MALFORMED;
        }
    }
}

static bool bs_coord_in_bounds(Coord coord, int width, int height) {
    return coord.x >= 0 && coord.x < width && coord.y >= 0 && coord.y < height;
}

static BsJsonStatus bs_validate_request(const BsParsedRequest* parsed) {
    if (parsed->board.width <= 0 || parsed->board.height <= 0) {
        return BS_JSON_MALFORMED;
    }
    if (parsed->timeout_ms < 0 || parsed->hazard_damage < 0) {
        return BS_JSON_MALFORMED;
    }

    for (BsSnakeNode* snake = parsed->board.snakes_head; snake != NULL; snake = snake->next) {
        if (snake->health < 0 || snake->length < 0) {
            return BS_JSON_MALFORMED;
        }
        for (int i = 0; i < snake->body_len; i++) {
            if (!bs_coord_in_bounds(snake->body[i], parsed->board.width, parsed->board.height)) {
                return BS_JSON_MALFORMED;
            }
        }
    }

    for (BsCoordNode* food = parsed->board.food_head; food != NULL; food = food->next) {
        if (!bs_coord_in_bounds(food->coord, parsed->board.width, parsed->board.height)) {
            return BS_JSON_MALFORMED;
        }
    }

    for (BsCoordNode* hazard = parsed->board.hazards_head; hazard != NULL; hazard = hazard->next) {
        if (!bs_coord_in_bounds(hazard->coord, parsed->board.width, parsed->board.height)) {
            return BS_JSON_MALFORMED;
        }
    }

    return BS_JSON_OK;
}

static BsJsonStatus bs_build_board(const BsParsedRequest* parsed, Board** out_board) {
    Board* board = BoardCreate(
        parsed->board.width,
        parsed->board.height,
        parsed->ruleset_name,
        parsed->hazard_damage
    );
    if (board == NULL) {
        return BS_JSON_NO_MEMORY;
    }

    for (BsSnakeNode* node = parsed->board.snakes_head; node != NULL; node = node->next) {
        Snake snake = {
            .id = node->id,
            .name = node->name,
            .health = node->health,
            .body = node->body,
            .body_len = node->body_len,
            .length = node->length,
        };
        if (!BoardAddSnake(board, &snake)) {
            BoardFree(board);
            return BS_JSON_NO_MEMORY;
        }
        Snake* added = &board->snakes[board->snake_count - 1];
        if (added->id == NULL ||
            added->name == NULL ||
            (node->body_len > 0 && (added->body == NULL || added->body_len != node->body_len))) {
            BoardFree(board);
            return BS_JSON_NO_MEMORY;
        }
        added->length = node->length;
    }

    for (BsCoordNode* node = parsed->board.food_head; node != NULL; node = node->next) {
        if (!BoardAddFood(board, node->coord)) {
            BoardFree(board);
            return BS_JSON_NO_MEMORY;
        }
    }

    for (BsCoordNode* node = parsed->board.hazards_head; node != NULL; node = node->next) {
        if (!BoardAddHazard(board, node->coord)) {
            BoardFree(board);
            return BS_JSON_NO_MEMORY;
        }
    }

    *out_board = board;
    return BS_JSON_OK;
}

BsJsonStatus BsParseGameRequest(
    const char* body,
    size_t body_len,
    BsArena* arena,
    BsGameRequest* out_request
) {
    if (body == NULL || arena == NULL || out_request == NULL) {
        return BS_JSON_MALFORMED;
    }

    *out_request = (BsGameRequest){0};

    char* default_ruleset = BsArenaStrDup(arena, "standard", strlen("standard"));
    if (default_ruleset == NULL) {
        return BS_JSON_NO_MEMORY;
    }

    BsParsedRequest parsed = {
        .turn = 0,
        .timeout_ms = 500,
        .ruleset_name = default_ruleset,
        .hazard_damage = 15,
    };

    BsJsonParser parser = {
        .cur = body,
        .end = body + body_len,
        .arena = arena,
    };

    if (!bs_consume(&parser, '{')) {
        return BS_JSON_MALFORMED;
    }
    bs_skip_ws(&parser);
    if (bs_consume(&parser, '}')) {
        return BS_JSON_MISSING_REQUIRED;
    }

    while (true) {
        char* key = NULL;
        BsJsonStatus status = bs_parse_string(&parser, &key);
        if (status != BS_JSON_OK) {
            return status;
        }
        if (!bs_consume(&parser, ':')) {
            return BS_JSON_MALFORMED;
        }

        if (strcmp(key, "game") == 0) {
            status = bs_parse_game(&parser, &parsed);
        } else if (strcmp(key, "turn") == 0) {
            bool is_null = false;
            status = bs_parse_nullable_integer(&parser, &parsed.turn, &is_null);
            if (status == BS_JSON_OK && is_null) {
                parsed.turn = 0;
            }
        } else if (strcmp(key, "board") == 0) {
            status = bs_parse_board(&parser, &parsed.board);
        } else if (strcmp(key, "you") == 0) {
            status = bs_parse_you(&parser, &parsed);
        } else {
            status = bs_skip_value(&parser, 0);
        }
        if (status != BS_JSON_OK) {
            return status;
        }

        bs_skip_ws(&parser);
        if (bs_consume(&parser, '}')) {
            break;
        }
        if (!bs_consume(&parser, ',')) {
            return BS_JSON_MALFORMED;
        }
    }

    bs_skip_ws(&parser);
    if (parser.cur != parser.end) {
        return BS_JSON_MALFORMED;
    }

    if (!parsed.has_game_id ||
        !parsed.board.has_width ||
        !parsed.board.has_height ||
        !parsed.board.has_snakes ||
        !parsed.has_you_id) {
        return BS_JSON_MISSING_REQUIRED;
    }

    BsJsonStatus status = bs_validate_request(&parsed);
    if (status != BS_JSON_OK) {
        return status;
    }

    Board* board = NULL;
    status = bs_build_board(&parsed, &board);
    if (status != BS_JSON_OK) {
        return status;
    }

    out_request->game_id = parsed.game_id;
    out_request->turn = parsed.turn;
    out_request->you_id = parsed.you_id;
    out_request->timeout_ms = parsed.timeout_ms;
    out_request->board = board;
    return BS_JSON_OK;
}

void BsGameRequestFree(BsGameRequest* request) {
    if (request == NULL) {
        return;
    }
    BoardFree(request->board);
    request->game_id = NULL;
    request->turn = 0;
    request->you_id = NULL;
    request->board = NULL;
    request->timeout_ms = 0;
}

const char* BsJsonStatusText(BsJsonStatus status) {
    switch (status) {
        case BS_JSON_OK:
            return "ok";
        case BS_JSON_MALFORMED:
            return "malformed json";
        case BS_JSON_MISSING_REQUIRED:
            return "missing required field";
        case BS_JSON_NO_MEMORY:
            return "out of memory";
    }
    return "unknown json status";
}
