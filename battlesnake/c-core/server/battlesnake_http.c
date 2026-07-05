#include "battlesnake_http.h"

#include "battlesnake_json.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

typedef struct {
    const char* start;
    size_t len;
} BsHttpSlice;

typedef enum {
    BS_HTTP_ROUTE_UNKNOWN = 0,
    BS_HTTP_ROUTE_INFO = 1,
    BS_HTTP_ROUTE_START = 2,
    BS_HTTP_ROUTE_MOVE = 3,
    BS_HTTP_ROUTE_END = 4,
} BsHttpRoute;

typedef struct {
    BsHttpSlice method;
    BsHttpSlice path;
    const char* version;
    size_t body_offset;
    size_t content_length;
    bool has_content_length;
    bool chunked_transfer;
    bool malformed;
    bool too_large;
} BsHttpRequestView;

enum {
    BS_HTTP_MAX_HEADER_BYTES = 16 * 1024,
    BS_HTTP_MAX_BODY_BYTES = 128 * 1024,
    BS_HTTP_MAX_REQUEST_BYTES = BS_HTTP_MAX_HEADER_BYTES + BS_HTTP_MAX_BODY_BYTES,
};

static int bs_ascii_tolower(int ch) {
    if (ch >= 'A' && ch <= 'Z') {
        return ch - 'A' + 'a';
    }
    return ch;
}

static bool bs_slice_equals(BsHttpSlice slice, const char* text) {
    size_t text_len = strlen(text);
    return slice.len == text_len && memcmp(slice.start, text, text_len) == 0;
}

static bool bs_case_equal_n(const char* lhs, size_t lhs_len, const char* rhs) {
    size_t rhs_len = strlen(rhs);
    if (lhs_len != rhs_len) {
        return false;
    }
    for (size_t i = 0; i < lhs_len; i++) {
        if (bs_ascii_tolower((unsigned char)lhs[i]) != bs_ascii_tolower((unsigned char)rhs[i])) {
            return false;
        }
    }
    return true;
}

static BsHttpSlice bs_trim_ascii_space(const char* start, const char* end) {
    while (start < end && (*start == ' ' || *start == '\t')) {
        start++;
    }
    while (end > start && (end[-1] == ' ' || end[-1] == '\t')) {
        end--;
    }
    return (BsHttpSlice){start, (size_t)(end - start)};
}

static bool bs_parse_size_t(BsHttpSlice value, size_t* out_value) {
    if (out_value == 0 || value.len == 0) {
        return false;
    }

    size_t parsed = 0;
    for (size_t i = 0; i < value.len; i++) {
        unsigned char ch = (unsigned char)value.start[i];
        if (ch < '0' || ch > '9') {
            return false;
        }
        size_t digit = (size_t)(ch - '0');
        if (parsed > (SIZE_MAX - digit) / 10u) {
            return false;
        }
        parsed = parsed * 10u + digit;
    }

    *out_value = parsed;
    return true;
}

static bool bs_transfer_encoding_has_chunked(BsHttpSlice value) {
    const char* cursor = value.start;
    const char* end = value.start + value.len;

    while (cursor < end) {
        const char* token_end = cursor;
        while (token_end < end && *token_end != ',') {
            token_end++;
        }
        BsHttpSlice token = bs_trim_ascii_space(cursor, token_end);
        if (bs_case_equal_n(token.start, token.len, "chunked")) {
            return true;
        }
        cursor = token_end < end ? token_end + 1 : end;
    }

    return false;
}

static const char* bs_reason_phrase(int status_code) {
    switch (status_code) {
        case 200:
            return "OK";
        case 400:
            return "Bad Request";
        case 404:
            return "Not Found";
        case 405:
            return "Method Not Allowed";
        case 413:
            return "Payload Too Large";
        case 500:
            return "Internal Server Error";
        case 501:
            return "Not Implemented";
        default:
            return "Internal Server Error";
    }
}

static BsHttpResult bs_write_response(
    const char* version,
    int status_code,
    const char* body,
    char* response,
    size_t response_capacity
) {
    if (version == 0) {
        version = "HTTP/1.1";
    }
    if (body == 0 || response == 0 || response_capacity == 0) {
        if (response != 0 && response_capacity > 0) {
            response[0] = '\0';
        }
        return (BsHttpResult){500, 0};
    }

    size_t body_len = strlen(body);
    int written = snprintf(
        response,
        response_capacity,
        "%s %d %s\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n"
        "Content-Length: %zu\r\n"
        "\r\n"
        "%s",
        version,
        status_code,
        bs_reason_phrase(status_code),
        body_len,
        body
    );
    if (written < 0 || (size_t)written >= response_capacity) {
        response[0] = '\0';
        return (BsHttpResult){500, 0};
    }

    return (BsHttpResult){status_code, (size_t)written};
}

static ssize_t bs_find_crlf(const char* data, size_t len, size_t start) {
    if (data == 0 || start >= len) {
        return -1;
    }
    for (size_t i = start; i + 1 < len; i++) {
        if (data[i] == '\r' && data[i + 1] == '\n') {
            return (ssize_t)i;
        }
    }
    return -1;
}

static ssize_t bs_find_header_terminator(const char* data, size_t len) {
    if (data == 0 || len < 4) {
        return -1;
    }
    for (size_t i = 0; i + 3 < len; i++) {
        if (data[i] == '\r' && data[i + 1] == '\n' && data[i + 2] == '\r' && data[i + 3] == '\n') {
            return (ssize_t)i;
        }
    }
    return -1;
}

static bool bs_parse_request_line(
    const char* request,
    size_t line_end,
    BsHttpSlice* out_method,
    BsHttpSlice* out_path,
    const char** out_version
) {
    const char* line = request;
    const char* end = request + line_end;
    const char* first_space = memchr(line, ' ', (size_t)(end - line));
    if (first_space == 0 || first_space == line) {
        return false;
    }
    const char* second_space = memchr(first_space + 1, ' ', (size_t)(end - (first_space + 1)));
    if (second_space == 0 || second_space == first_space + 1) {
        return false;
    }
    if (memchr(second_space + 1, ' ', (size_t)(end - (second_space + 1))) != 0) {
        return false;
    }

    BsHttpSlice method = {line, (size_t)(first_space - line)};
    BsHttpSlice path = {first_space + 1, (size_t)(second_space - (first_space + 1))};
    BsHttpSlice version = {second_space + 1, (size_t)(end - (second_space + 1))};
    if (path.len == 0) {
        return false;
    }
    if (bs_slice_equals(version, "HTTP/1.0")) {
        *out_version = "HTTP/1.0";
    } else if (bs_slice_equals(version, "HTTP/1.1")) {
        *out_version = "HTTP/1.1";
    } else {
        return false;
    }

    *out_method = method;
    *out_path = path;
    return true;
}

static BsHttpRequestView bs_parse_request(const char* request, size_t request_len) {
    BsHttpRequestView view;
    memset(&view, 0, sizeof(view));

    if (request_len > BS_HTTP_MAX_REQUEST_BYTES) {
        view.too_large = true;
        return view;
    }

    size_t header_scan_len = request_len;
    if (header_scan_len > BS_HTTP_MAX_HEADER_BYTES) {
        header_scan_len = BS_HTTP_MAX_HEADER_BYTES;
    }

    ssize_t header_end = bs_find_header_terminator(request, header_scan_len);
    if (header_end < 0) {
        if (request_len > BS_HTTP_MAX_HEADER_BYTES) {
            view.too_large = true;
            return view;
        }
        view.malformed = true;
        return view;
    }

    ssize_t request_line_end = bs_find_crlf(request, request_len, 0);
    if (request_line_end < 0 || request_line_end >= header_end) {
        view.malformed = true;
        return view;
    }
    if (!bs_parse_request_line(request, (size_t)request_line_end, &view.method, &view.path, &view.version)) {
        view.malformed = true;
        return view;
    }

    size_t line_start = (size_t)request_line_end + 2u;
    while (line_start < (size_t)header_end) {
        ssize_t line_end = bs_find_crlf(request, request_len, line_start);
        if (line_end < 0 || line_end > header_end) {
            view.malformed = true;
            return view;
        }
        if ((size_t)line_end == line_start) {
            break;
        }

        const char* line = request + line_start;
        const char* line_stop = request + line_end;
        const char* colon = memchr(line, ':', (size_t)(line_stop - line));
        if (colon == 0 || colon == line) {
            view.malformed = true;
            return view;
        }

        BsHttpSlice name = {line, (size_t)(colon - line)};
        BsHttpSlice value = bs_trim_ascii_space(colon + 1, line_stop);

        if (bs_case_equal_n(name.start, name.len, "content-length")) {
            size_t parsed_length = 0;
            if (!bs_parse_size_t(value, &parsed_length)) {
                view.malformed = true;
                return view;
            }
            if (view.has_content_length && view.content_length != parsed_length) {
                view.malformed = true;
                return view;
            }
            view.has_content_length = true;
            view.content_length = parsed_length;
            if (view.content_length > BS_HTTP_MAX_BODY_BYTES) {
                view.too_large = true;
                return view;
            }
        } else if (bs_case_equal_n(name.start, name.len, "transfer-encoding")) {
            if (bs_transfer_encoding_has_chunked(value)) {
                view.chunked_transfer = true;
            }
        }

        line_start = (size_t)line_end + 2u;
    }

    view.body_offset = (size_t)header_end + 4u;
    return view;
}

static BsHttpRoute bs_route_from_path(BsHttpSlice path) {
    if (bs_slice_equals(path, "/")) {
        return BS_HTTP_ROUTE_INFO;
    }
    if (bs_slice_equals(path, "/start")) {
        return BS_HTTP_ROUTE_START;
    }
    if (bs_slice_equals(path, "/move")) {
        return BS_HTTP_ROUTE_MOVE;
    }
    if (bs_slice_equals(path, "/end")) {
        return BS_HTTP_ROUTE_END;
    }
    return BS_HTTP_ROUTE_UNKNOWN;
}

BsHttpResult BsHandleHttpRequest(
    const char* request,
    size_t request_len,
    BsArena* arena,
    const BsStrategyConfig* strategy_config,
    char* response,
    size_t response_capacity
) {
    static const char* info_body =
        "{\"apiversion\":\"1\",\"author\":\"codex\",\"color\":\"#2563eb\",\"head\":\"default\",\"tail\":\"default\",\"version\":\"0.1.0-native\"}";
    static const char* empty_body = "{}";

    if (request == 0 || arena == 0) {
        return bs_write_response("HTTP/1.1", 500, empty_body, response, response_capacity);
    }

    BsHttpRequestView parsed = bs_parse_request(request, request_len);
    if (parsed.too_large) {
        return bs_write_response(parsed.version, 413, empty_body, response, response_capacity);
    }
    if (parsed.malformed) {
        return bs_write_response(parsed.version, 400, empty_body, response, response_capacity);
    }
    if (parsed.chunked_transfer) {
        return bs_write_response(parsed.version, 501, empty_body, response, response_capacity);
    }

    BsHttpRoute route = bs_route_from_path(parsed.path);
    if (route == BS_HTTP_ROUTE_UNKNOWN) {
        return bs_write_response(parsed.version, 404, empty_body, response, response_capacity);
    }

    bool is_get = bs_slice_equals(parsed.method, "GET");
    bool is_post = bs_slice_equals(parsed.method, "POST");
    if ((route == BS_HTTP_ROUTE_INFO && !is_get) || (route != BS_HTTP_ROUTE_INFO && !is_post)) {
        return bs_write_response(parsed.version, 405, empty_body, response, response_capacity);
    }
    if (!is_get && !is_post) {
        return bs_write_response(parsed.version, 405, empty_body, response, response_capacity);
    }

    if (route == BS_HTTP_ROUTE_INFO) {
        if (parsed.body_offset != request_len) {
            return bs_write_response(parsed.version, 400, empty_body, response, response_capacity);
        }
        return bs_write_response(parsed.version, 200, info_body, response, response_capacity);
    }

    if (!parsed.has_content_length) {
        return bs_write_response(parsed.version, 400, empty_body, response, response_capacity);
    }
    if (parsed.body_offset > request_len || parsed.content_length > request_len - parsed.body_offset) {
        return bs_write_response(parsed.version, 400, empty_body, response, response_capacity);
    }
    if (parsed.body_offset + parsed.content_length != request_len) {
        return bs_write_response(parsed.version, 400, empty_body, response, response_capacity);
    }

    BsArenaReset(arena);

    BsGameRequest game_request;
    memset(&game_request, 0, sizeof(game_request));
    BsJsonStatus parse_status = BsParseGameRequest(
        request + parsed.body_offset,
        parsed.content_length,
        arena,
        &game_request
    );
    if (parse_status == BS_JSON_MALFORMED || parse_status == BS_JSON_MISSING_REQUIRED) {
        return bs_write_response(parsed.version, 400, empty_body, response, response_capacity);
    }
    if (parse_status != BS_JSON_OK || BsArenaHadOverflow(arena)) {
        return bs_write_response(parsed.version, 500, empty_body, response, response_capacity);
    }

    if (route == BS_HTTP_ROUTE_START || route == BS_HTTP_ROUTE_END) {
        BsGameRequestFree(&game_request);
        return bs_write_response(parsed.version, 200, empty_body, response, response_capacity);
    }

    MoveDirection move = MOVE_INVALID;
    BsStrategyStatus strategy_status = BsChooseMove(
        game_request.board,
        game_request.you_id,
        strategy_config,
        &move
    );
    if (strategy_status == BS_STRATEGY_ERROR) {
        BsGameRequestFree(&game_request);
        return bs_write_response(parsed.version, 500, empty_body, response, response_capacity);
    }

    const char* move_text = MoveDirectionToString(move);
    if (move_text == 0 || move_text[0] == '\0') {
        BsGameRequestFree(&game_request);
        return bs_write_response(parsed.version, 500, empty_body, response, response_capacity);
    }

    char body[32];
    int body_written = snprintf(body, sizeof(body), "{\"move\":\"%s\"}", move_text);
    BsGameRequestFree(&game_request);
    if (body_written < 0 || (size_t)body_written >= sizeof(body)) {
        return bs_write_response(parsed.version, 500, empty_body, response, response_capacity);
    }

    return bs_write_response(parsed.version, 200, body, response, response_capacity);
}
