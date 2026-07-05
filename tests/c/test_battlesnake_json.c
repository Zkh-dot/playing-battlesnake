#include "../../battlesnake/c-core/server/battlesnake_json.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char* MOVE_BODY =
    "{"
    "\"game\":{\"id\":\"game-1\",\"ruleset\":{\"name\":\"solo\",\"version\":\"v1\",\"settings\":{\"hazardDamagePerTurn\":14}},\"timeout\":500},"
    "\"turn\":14,"
    "\"board\":{\"height\":7,\"width\":7,\"food\":[{\"x\":3,\"y\":3}],\"hazards\":[{\"x\":0,\"y\":0}],"
    "\"snakes\":["
    "{\"id\":\"me\",\"name\":\"Me\",\"health\":90,\"body\":[{\"x\":1,\"y\":3},{\"x\":1,\"y\":2},{\"x\":1,\"y\":1}],\"head\":{\"x\":1,\"y\":3},\"length\":3},"
    "{\"id\":\"you\",\"name\":\"You\",\"health\":88,\"body\":[{\"x\":5,\"y\":3},{\"x\":5,\"y\":2},{\"x\":5,\"y\":1}],\"head\":{\"x\":5,\"y\":3},\"length\":3}"
    "]},"
    "\"you\":{\"id\":\"me\",\"name\":\"Me\",\"health\":90,\"body\":[{\"x\":1,\"y\":3},{\"x\":1,\"y\":2},{\"x\":1,\"y\":1}],\"head\":{\"x\":1,\"y\":3},\"length\":3}"
    "}";

static void test_parse_complete_move_body(void) {
    BsArena arena;
    BsGameRequest request;
    assert(BsArenaInit(&arena, 65536));
    assert(BsParseGameRequest(MOVE_BODY, strlen(MOVE_BODY), &arena, &request) == BS_JSON_OK);
    assert(strcmp(request.game_id, "game-1") == 0);
    assert(strcmp(request.you_id, "me") == 0);
    assert(request.turn == 14);
    assert(request.timeout_ms == 500);
    assert(request.board != 0);
    assert(request.board->width == 7);
    assert(request.board->height == 7);
    assert(strcmp(request.board->ruleset_name, "solo") == 0);
    assert(request.board->hazard_damage == 14);
    assert(request.board->snake_count == 2);
    assert(request.board->food_count == 1);
    assert(request.board->hazard_count == 1);
    assert(strcmp(request.board->snakes[0].id, "me") == 0);
    assert(strcmp(request.board->snakes[0].name, "Me") == 0);
    assert(request.board->snakes[0].health == 90);
    assert(request.board->snakes[0].body_len == 3);
    assert(request.board->snakes[0].length == 3);
    assert(request.board->snakes[0].body[0].x == 1);
    assert(request.board->snakes[0].body[0].y == 3);
    BsGameRequestFree(&request);
    BsArenaFree(&arena);
}

static void test_missing_you_id_is_rejected(void) {
    const char* body = "{\"game\":{\"id\":\"g\"},\"turn\":0,\"board\":{\"height\":1,\"width\":1,\"snakes\":[]},\"you\":{}}";
    BsArena arena;
    BsGameRequest request;
    assert(BsArenaInit(&arena, 4096));
    assert(BsParseGameRequest(body, strlen(body), &arena, &request) == BS_JSON_MISSING_REQUIRED);
    BsArenaFree(&arena);
}

static void test_malformed_body_is_rejected(void) {
    const char* body = "{\"game\":";
    BsArena arena;
    BsGameRequest request;
    assert(BsArenaInit(&arena, 4096));
    assert(BsParseGameRequest(body, strlen(body), &arena, &request) == BS_JSON_MALFORMED);
    BsArenaFree(&arena);
}

static void test_unknown_fields_are_skipped(void) {
    const char* body =
        "{"
        "\"game\":{\"id\":\"g\",\"ruleset\":{\"settings\":{\"hazardDamagePerTurn\":21,\"ignoredFloat\":1.25}},\"timeout\":null,\"meta\":{\"nested\":[true,false,null,{\"value\":2.5}]}}"
        ",\"turn\":null,"
        "\"board\":{\"height\":3,\"width\":3,\"snakes\":[{\"id\":\"me\",\"body\":[{\"x\":1,\"y\":1},{\"x\":1,\"y\":0}],\"latency\":\"5\",\"customizations\":{\"color\":\"#fff\"}}],\"mystery\":{\"flag\":true,\"items\":[1,2,3.5]}}"
        ",\"you\":{\"id\":\"me\",\"unused\":{\"deep\":[{\"x\":0.5}]}}"
        "}";
    BsArena arena;
    BsGameRequest request;
    assert(BsArenaInit(&arena, 16384));
    assert(BsParseGameRequest(body, strlen(body), &arena, &request) == BS_JSON_OK);
    assert(request.turn == 0);
    assert(request.timeout_ms == 500);
    assert(request.board != NULL);
    assert(strcmp(request.board->ruleset_name, "standard") == 0);
    assert(request.board->hazard_damage == 21);
    assert(request.board->snake_count == 1);
    assert(strcmp(request.board->snakes[0].id, "me") == 0);
    assert(strcmp(request.board->snakes[0].name, "") == 0);
    assert(request.board->snakes[0].health == 100);
    assert(request.board->snakes[0].length == 2);
    BsGameRequestFree(&request);
    BsArenaFree(&arena);
}

static void test_absent_food_and_hazards_default_to_zero_counts(void) {
    const char* body =
        "{"
        "\"game\":{\"id\":\"g\",\"ruleset\":{},\"timeout\":400},"
        "\"board\":{\"width\":2,\"height\":2,\"snakes\":[{\"id\":\"me\",\"body\":[]}]},"
        "\"you\":{\"id\":\"me\"}"
        "}";
    BsArena arena;
    BsGameRequest request;
    assert(BsArenaInit(&arena, 8192));
    assert(BsParseGameRequest(body, strlen(body), &arena, &request) == BS_JSON_OK);
    assert(request.timeout_ms == 400);
    assert(request.board != NULL);
    assert(strcmp(request.board->ruleset_name, "standard") == 0);
    assert(request.board->hazard_damage == 15);
    assert(request.board->food_count == 0);
    assert(request.board->hazard_count == 0);
    assert(request.board->snake_count == 1);
    assert(request.board->snakes[0].body_len == 0);
    assert(request.board->snakes[0].length == 0);
    BsGameRequestFree(&request);
    BsArenaFree(&arena);
}

static void test_zero_or_negative_dimensions_are_rejected(void) {
    const char* zero_width =
        "{"
        "\"game\":{\"id\":\"g\"},"
        "\"board\":{\"width\":0,\"height\":2,\"snakes\":[{\"id\":\"me\",\"body\":[]}]},"
        "\"you\":{\"id\":\"me\"}"
        "}";
    const char* negative_height =
        "{"
        "\"game\":{\"id\":\"g\"},"
        "\"board\":{\"width\":2,\"height\":-1,\"snakes\":[{\"id\":\"me\",\"body\":[]}]},"
        "\"you\":{\"id\":\"me\"}"
        "}";
    BsArena arena;
    BsGameRequest request;

    assert(BsArenaInit(&arena, 4096));
    assert(BsParseGameRequest(zero_width, strlen(zero_width), &arena, &request) == BS_JSON_MALFORMED);
    BsArenaReset(&arena);
    assert(BsParseGameRequest(negative_height, strlen(negative_height), &arena, &request) == BS_JSON_MALFORMED);
    BsArenaFree(&arena);
}

static void test_out_of_bounds_coordinates_are_rejected(void) {
    const char* snake_out_of_bounds =
        "{"
        "\"game\":{\"id\":\"g\"},"
        "\"board\":{\"width\":2,\"height\":2,\"snakes\":[{\"id\":\"me\",\"body\":[{\"x\":2,\"y\":0}]}]},"
        "\"you\":{\"id\":\"me\"}"
        "}";
    const char* food_out_of_bounds =
        "{"
        "\"game\":{\"id\":\"g\"},"
        "\"board\":{\"width\":2,\"height\":2,\"food\":[{\"x\":0,\"y\":2}],\"snakes\":[{\"id\":\"me\",\"body\":[]}]},"
        "\"you\":{\"id\":\"me\"}"
        "}";
    BsArena arena;
    BsGameRequest request;

    assert(BsArenaInit(&arena, 4096));
    assert(BsParseGameRequest(snake_out_of_bounds, strlen(snake_out_of_bounds), &arena, &request) == BS_JSON_MALFORMED);
    BsArenaReset(&arena);
    assert(BsParseGameRequest(food_out_of_bounds, strlen(food_out_of_bounds), &arena, &request) == BS_JSON_MALFORMED);
    BsArenaFree(&arena);
}

static void test_negative_semantic_numeric_fields_are_rejected(void) {
    const char* body =
        "{"
        "\"game\":{\"id\":\"g\",\"timeout\":-1,\"ruleset\":{\"settings\":{\"hazardDamagePerTurn\":-3}}},"
        "\"board\":{\"width\":2,\"height\":2,\"snakes\":[{\"id\":\"me\",\"health\":-5,\"length\":-1,\"body\":[]}]},"
        "\"you\":{\"id\":\"me\"}"
        "}";
    BsArena arena;
    BsGameRequest request;

    assert(BsArenaInit(&arena, 8192));
    assert(BsParseGameRequest(body, strlen(body), &arena, &request) == BS_JSON_MALFORMED);
    BsArenaFree(&arena);
}

static void test_large_ignored_string_does_not_consume_arena(void) {
    enum { IGNORE_LEN = 4096, BUFFER_LEN = 4600 };
    char ignored[IGNORE_LEN + 1];
    char body[BUFFER_LEN];
    memset(ignored, 'x', IGNORE_LEN);
    ignored[IGNORE_LEN] = '\0';

    int written = snprintf(
        body,
        sizeof(body),
        "{"
        "\"game\":{\"id\":\"g\",\"ignored\":\"%s\"},"
        "\"board\":{\"width\":2,\"height\":2,\"snakes\":[{\"id\":\"me\",\"body\":[]}]},"
        "\"you\":{\"id\":\"me\"}"
        "}",
        ignored
    );
    assert(written > 0);
    assert((size_t)written < sizeof(body));

    BsArena arena;
    BsGameRequest request;
    assert(BsArenaInit(&arena, 512));
    assert(BsParseGameRequest(body, (size_t)written, &arena, &request) == BS_JSON_OK);
    assert(strcmp(request.game_id, "g") == 0);
    BsGameRequestFree(&request);
    BsArenaFree(&arena);
}

static void test_deep_unknown_nesting_exceeds_cap(void) {
    enum { DEPTH = 129, BUFFER_LEN = 1024 };
    char body[BUFFER_LEN];
    size_t pos = 0;

    int written = snprintf(
        body,
        sizeof(body),
        "{"
        "\"game\":{\"id\":\"g\",\"ignored\":"
    );
    assert(written > 0);
    pos = (size_t)written;

    for (int i = 0; i < DEPTH; i++) {
        assert(pos + 1 < sizeof(body));
        body[pos++] = '[';
    }
    assert(pos + 1 < sizeof(body));
    body[pos++] = '0';
    for (int i = 0; i < DEPTH; i++) {
        assert(pos + 1 < sizeof(body));
        body[pos++] = ']';
    }

    written = snprintf(
        body + pos,
        sizeof(body) - pos,
        "},"
        "\"board\":{\"width\":2,\"height\":2,\"snakes\":[{\"id\":\"me\",\"body\":[]}]},"
        "\"you\":{\"id\":\"me\"}"
        "}"
    );
    assert(written > 0);
    pos += (size_t)written;

    BsArena arena;
    BsGameRequest request;
    assert(BsArenaInit(&arena, 4096));
    assert(BsParseGameRequest(body, pos, &arena, &request) == BS_JSON_MALFORMED);
    BsArenaFree(&arena);
}

static void test_status_texts(void) {
    assert(strcmp(BsJsonStatusText(BS_JSON_OK), "ok") == 0);
    assert(strcmp(BsJsonStatusText(BS_JSON_MALFORMED), "malformed json") == 0);
    assert(strcmp(BsJsonStatusText(BS_JSON_MISSING_REQUIRED), "missing required field") == 0);
    assert(strcmp(BsJsonStatusText(BS_JSON_NO_MEMORY), "out of memory") == 0);
    assert(strcmp(BsJsonStatusText((BsJsonStatus)99), "unknown json status") == 0);
}

int main(void) {
    test_parse_complete_move_body();
    test_missing_you_id_is_rejected();
    test_malformed_body_is_rejected();
    test_unknown_fields_are_skipped();
    test_absent_food_and_hazards_default_to_zero_counts();
    test_zero_or_negative_dimensions_are_rejected();
    test_out_of_bounds_coordinates_are_rejected();
    test_negative_semantic_numeric_fields_are_rejected();
    test_large_ignored_string_does_not_consume_arena();
    test_deep_unknown_nesting_exceeds_cap();
    test_status_texts();
    return 0;
}
