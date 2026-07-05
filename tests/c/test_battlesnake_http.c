#include "../../battlesnake/c-core/server/battlesnake_http.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* MOVE_BODY =
    "{\"game\":{\"id\":\"g\",\"ruleset\":{\"name\":\"standard\",\"settings\":{\"hazardDamagePerTurn\":0}},\"timeout\":500},"
    "\"turn\":1,"
    "\"board\":{\"height\":5,\"width\":5,\"snakes\":[{\"id\":\"me\",\"health\":90,\"body\":[{\"x\":2,\"y\":2},{\"x\":2,\"y\":1},{\"x\":2,\"y\":0}],\"length\":3}]},"
    "\"you\":{\"id\":\"me\",\"health\":90,\"body\":[{\"x\":2,\"y\":2},{\"x\":2,\"y\":1},{\"x\":2,\"y\":0}],\"length\":3}}";

static const char* MISSING_CONTROLLED_SNAKE_BODY =
    "{\"game\":{\"id\":\"g\",\"ruleset\":{\"name\":\"standard\",\"settings\":{\"hazardDamagePerTurn\":0}},\"timeout\":500},"
    "\"turn\":1,"
    "\"board\":{\"height\":5,\"width\":5,\"snakes\":[{\"id\":\"other\",\"health\":90,\"body\":[{\"x\":2,\"y\":2},{\"x\":2,\"y\":1},{\"x\":2,\"y\":0}],\"length\":3}]},"
    "\"you\":{\"id\":\"me\",\"health\":90,\"body\":[{\"x\":2,\"y\":2},{\"x\":2,\"y\":1},{\"x\":2,\"y\":0}],\"length\":3}}";

static const char* MISSING_REQUIRED_BODY =
    "{\"game\":{\"id\":\"g\"},\"turn\":0,\"board\":{\"height\":1,\"width\":1,\"snakes\":[]},\"you\":{}}";

static const char* MALFORMED_BODY = "{\"game\":";

static void request_from_body_with_headers(
    char* out,
    size_t out_size,
    const char* method,
    const char* path,
    const char* content_length_name,
    const char* extra_headers,
    const char* body
) {
    const char* length_name = content_length_name != 0 ? content_length_name : "Content-Length";
    const char* headers = extra_headers != 0 ? extra_headers : "";
    int written = snprintf(
        out,
        out_size,
        "%s %s HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Type: application/json\r\n"
        "%s: %zu\r\n"
        "%s"
        "\r\n"
        "%s",
        method,
        path,
        length_name,
        strlen(body),
        headers,
        body
    );
    assert(written > 0);
    assert((size_t)written < out_size);
}

static const char* response_body(const char* response) {
    const char* body = strstr(response, "\r\n\r\n");
    assert(body != 0);
    return body + 4;
}

static void assert_content_length_matches_body(const char* response) {
    const char* body = response_body(response);
    char expected[64];
    int written = snprintf(expected, sizeof(expected), "Content-Length: %zu", strlen(body));
    assert(written > 0);
    assert((size_t)written < sizeof(expected));
    assert(strstr(response, expected) != 0);
}

static void test_info_route(void) {
    BsArena arena;
    BsStrategyConfig config = BsStrategyConfigDefault();
    char response[2048];
    const char* request = "GET / HTTP/1.0\r\nHost: x\r\n\r\n";

    assert(BsArenaInit(&arena, 65536));
    BsHttpResult result = BsHandleHttpRequest(request, strlen(request), &arena, &config, response, sizeof(response));
    assert(result.status_code == 200);
    assert(result.response_len == strlen(response));
    assert(strstr(response, "HTTP/1.0 200 OK") != 0);
    assert(strstr(response, "\"apiversion\":\"1\"") != 0);
    assert(strstr(response, "Content-Type: application/json") != 0);
    assert(strstr(response, "Connection: close") != 0);
    assert_content_length_matches_body(response);
    BsArenaFree(&arena);
}

static void test_move_route(void) {
    BsArena arena;
    BsStrategyConfig config = BsStrategyConfigDefault();
    char request[4096];
    char response[2048];

    assert(BsArenaInit(&arena, 65536));
    request_from_body_with_headers(request, sizeof(request), "POST", "/move", "Content-Length", 0, MOVE_BODY);
    BsHttpResult result = BsHandleHttpRequest(request, strlen(request), &arena, &config, response, sizeof(response));
    assert(result.status_code == 200);
    assert(strstr(response, "HTTP/1.1 200 OK") != 0);
    assert(strstr(response, "\"move\":\"") != 0);
    assert_content_length_matches_body(response);
    BsArenaFree(&arena);
}

static void test_unknown_route(void) {
    BsArena arena;
    BsStrategyConfig config = BsStrategyConfigDefault();
    char response[2048];
    const char* request = "GET /bad HTTP/1.1\r\nHost: x\r\n\r\n";

    assert(BsArenaInit(&arena, 65536));
    BsHttpResult result = BsHandleHttpRequest(request, strlen(request), &arena, &config, response, sizeof(response));
    assert(result.status_code == 404);
    assert(strstr(response, "HTTP/1.1 404 Not Found") != 0);
    BsArenaFree(&arena);
}

static void test_post_without_content_length(void) {
    BsArena arena;
    BsStrategyConfig config = BsStrategyConfigDefault();
    char response[2048];
    const char* request = "POST /move HTTP/1.1\r\nHost: x\r\n\r\n{}";

    assert(BsArenaInit(&arena, 65536));
    BsHttpResult result = BsHandleHttpRequest(request, strlen(request), &arena, &config, response, sizeof(response));
    assert(result.status_code == 400);
    assert(strstr(response, "HTTP/1.1 400 Bad Request") != 0);
    BsArenaFree(&arena);
}

static void test_wrong_method_returns_405(void) {
    BsArena arena;
    BsStrategyConfig config = BsStrategyConfigDefault();
    char response[2048];
    const char* request = "GET /move HTTP/1.1\r\nHost: x\r\n\r\n";

    assert(BsArenaInit(&arena, 65536));
    BsHttpResult result = BsHandleHttpRequest(request, strlen(request), &arena, &config, response, sizeof(response));
    assert(result.status_code == 405);
    assert(strstr(response, "HTTP/1.1 405 Method Not Allowed") != 0);
    BsArenaFree(&arena);
}

static void test_lowercase_content_length_is_accepted(void) {
    BsArena arena;
    BsStrategyConfig config = BsStrategyConfigDefault();
    char request[4096];
    char response[2048];

    assert(BsArenaInit(&arena, 65536));
    request_from_body_with_headers(request, sizeof(request), "POST", "/move", "content-length", 0, MOVE_BODY);
    BsHttpResult result = BsHandleHttpRequest(request, strlen(request), &arena, &config, response, sizeof(response));
    assert(result.status_code == 200);
    assert(strstr(response, "\"move\":\"") != 0);
    BsArenaFree(&arena);
}

static void test_chunked_transfer_returns_501(void) {
    BsArena arena;
    BsStrategyConfig config = BsStrategyConfigDefault();
    char response[2048];
    const char* request =
        "POST /move HTTP/1.1\r\n"
        "Host: x\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "4\r\n{}\r\n0\r\n\r\n";

    assert(BsArenaInit(&arena, 65536));
    BsHttpResult result = BsHandleHttpRequest(request, strlen(request), &arena, &config, response, sizeof(response));
    assert(result.status_code == 501);
    assert(strstr(response, "HTTP/1.1 501 Not Implemented") != 0);
    BsArenaFree(&arena);
}

static void test_strategy_error_returns_500(void) {
    BsArena arena;
    BsStrategyConfig config = BsStrategyConfigDefault();
    char request[4096];
    char response[2048];

    assert(BsArenaInit(&arena, 65536));
    request_from_body_with_headers(
        request,
        sizeof(request),
        "POST",
        "/move",
        "Content-Length",
        0,
        MISSING_CONTROLLED_SNAKE_BODY
    );
    BsHttpResult result = BsHandleHttpRequest(request, strlen(request), &arena, &config, response, sizeof(response));
    assert(result.status_code == 500);
    assert(strstr(response, "HTTP/1.1 500 Internal Server Error") != 0);
    BsArenaFree(&arena);
}

static void test_malformed_json_returns_400(void) {
    BsArena arena;
    BsStrategyConfig config = BsStrategyConfigDefault();
    char request[1024];
    char response[2048];

    assert(BsArenaInit(&arena, 65536));

    request_from_body_with_headers(request, sizeof(request), "POST", "/start", "Content-Length", 0, MALFORMED_BODY);
    BsHttpResult start_result = BsHandleHttpRequest(request, strlen(request), &arena, &config, response, sizeof(response));
    assert(start_result.status_code == 400);
    assert(strstr(response, "HTTP/1.1 400 Bad Request") != 0);

    request_from_body_with_headers(request, sizeof(request), "POST", "/move", "Content-Length", 0, MALFORMED_BODY);
    BsHttpResult move_result = BsHandleHttpRequest(request, strlen(request), &arena, &config, response, sizeof(response));
    assert(move_result.status_code == 400);
    assert(strstr(response, "HTTP/1.1 400 Bad Request") != 0);

    BsArenaFree(&arena);
}

static void test_missing_required_json_returns_400(void) {
    BsArena arena;
    BsStrategyConfig config = BsStrategyConfigDefault();
    char request[1024];
    char response[2048];

    assert(BsArenaInit(&arena, 65536));

    request_from_body_with_headers(request, sizeof(request), "POST", "/end", "Content-Length", 0, MISSING_REQUIRED_BODY);
    BsHttpResult end_result = BsHandleHttpRequest(request, strlen(request), &arena, &config, response, sizeof(response));
    assert(end_result.status_code == 400);
    assert(strstr(response, "HTTP/1.1 400 Bad Request") != 0);

    request_from_body_with_headers(request, sizeof(request), "POST", "/move", "Content-Length", 0, MISSING_REQUIRED_BODY);
    BsHttpResult move_result = BsHandleHttpRequest(request, strlen(request), &arena, &config, response, sizeof(response));
    assert(move_result.status_code == 400);
    assert(strstr(response, "HTTP/1.1 400 Bad Request") != 0);

    BsArenaFree(&arena);
}

static void test_post_with_appended_second_request_returns_400(void) {
    BsArena arena;
    BsStrategyConfig config = BsStrategyConfigDefault();
    char request[8192];
    char response[2048];
    const char* second_request = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";

    assert(BsArenaInit(&arena, 65536));
    request_from_body_with_headers(request, sizeof(request), "POST", "/move", "Content-Length", 0, MOVE_BODY);
    assert(strlen(request) + strlen(second_request) + 1 < sizeof(request));
    strcat(request, second_request);

    BsHttpResult result = BsHandleHttpRequest(request, strlen(request), &arena, &config, response, sizeof(response));
    assert(result.status_code == 400);
    assert(strstr(response, "HTTP/1.1 400 Bad Request") != 0);
    BsArenaFree(&arena);
}

static void test_get_with_stray_bytes_returns_400(void) {
    BsArena arena;
    BsStrategyConfig config = BsStrategyConfigDefault();
    char response[2048];
    const char* request = "GET / HTTP/1.1\r\nHost: x\r\n\r\njunk";

    assert(BsArenaInit(&arena, 65536));
    BsHttpResult result = BsHandleHttpRequest(request, strlen(request), &arena, &config, response, sizeof(response));
    assert(result.status_code == 400);
    assert(strstr(response, "HTTP/1.1 400 Bad Request") != 0);
    BsArenaFree(&arena);
}

static void test_content_length_just_over_limit_returns_413(void) {
    BsArena arena;
    BsStrategyConfig config = BsStrategyConfigDefault();
    char response[2048];
    char request[256];

    assert(BsArenaInit(&arena, 65536));
    int written = snprintf(
        request,
        sizeof(request),
        "POST /move HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 131073\r\n"
        "\r\n"
    );
    assert(written > 0);
    assert((size_t)written < sizeof(request));

    BsHttpResult result = BsHandleHttpRequest(request, (size_t)written, &arena, &config, response, sizeof(response));
    assert(result.status_code == 413);
    assert(strstr(response, "HTTP/1.1 413 Payload Too Large") != 0);
    BsArenaFree(&arena);
}

static void test_tiny_response_capacity_returns_500_without_overflow(void) {
    BsArena arena;
    BsStrategyConfig config = BsStrategyConfigDefault();
    char response[8];
    const char* success_request = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";
    const char* error_request = "POST /move HTTP/1.1\r\nHost: x\r\n\r\n{}";

    assert(BsArenaInit(&arena, 65536));

    memset(response, 'x', sizeof(response));
    BsHttpResult success_result = BsHandleHttpRequest(
        success_request,
        strlen(success_request),
        &arena,
        &config,
        response,
        sizeof(response)
    );
    assert(success_result.status_code == 500);
    assert(success_result.response_len == 0);
    assert(response[0] == '\0');

    memset(response, 'x', sizeof(response));
    BsHttpResult error_result = BsHandleHttpRequest(
        error_request,
        strlen(error_request),
        &arena,
        &config,
        response,
        sizeof(response)
    );
    assert(error_result.status_code == 500);
    assert(error_result.response_len == 0);
    assert(response[0] == '\0');

    BsArenaFree(&arena);
}

static void test_duplicate_content_length_handling(void) {
    BsArena arena;
    BsStrategyConfig config = BsStrategyConfigDefault();
    char request[4096];
    char response[2048];
    char identical_headers[128];
    char conflicting_headers[128];

    assert(BsArenaInit(&arena, 65536));

    int identical_written = snprintf(
        identical_headers,
        sizeof(identical_headers),
        "Content-Length: %zu\r\n",
        strlen(MOVE_BODY)
    );
    assert(identical_written > 0);
    assert((size_t)identical_written < sizeof(identical_headers));
    request_from_body_with_headers(
        request,
        sizeof(request),
        "POST",
        "/move",
        "Content-Length",
        identical_headers,
        MOVE_BODY
    );
    BsHttpResult identical_result = BsHandleHttpRequest(request, strlen(request), &arena, &config, response, sizeof(response));
    assert(identical_result.status_code == 200);
    assert(strstr(response, "\"move\":\"") != 0);

    int conflicting_written = snprintf(
        conflicting_headers,
        sizeof(conflicting_headers),
        "Content-Length: %zu\r\n",
        strlen(MOVE_BODY) + 1u
    );
    assert(conflicting_written > 0);
    assert((size_t)conflicting_written < sizeof(conflicting_headers));
    request_from_body_with_headers(
        request,
        sizeof(request),
        "POST",
        "/move",
        "Content-Length",
        conflicting_headers,
        MOVE_BODY
    );
    BsHttpResult conflicting_result = BsHandleHttpRequest(request, strlen(request), &arena, &config, response, sizeof(response));
    assert(conflicting_result.status_code == 400);
    assert(strstr(response, "HTTP/1.1 400 Bad Request") != 0);

    BsArenaFree(&arena);
}

static void test_malformed_header_syntax_returns_400(void) {
    BsArena arena;
    BsStrategyConfig config = BsStrategyConfigDefault();
    char response[2048];
    const char* request =
        "POST /move HTTP/1.1\r\n"
        "Host localhost\r\n"
        "Content-Length: 2\r\n"
        "\r\n"
        "{}";

    assert(BsArenaInit(&arena, 65536));
    BsHttpResult result = BsHandleHttpRequest(request, strlen(request), &arena, &config, response, sizeof(response));
    assert(result.status_code == 400);
    assert(strstr(response, "HTTP/1.1 400 Bad Request") != 0);
    BsArenaFree(&arena);
}

static void test_short_body_mismatch_returns_400(void) {
    BsArena arena;
    BsStrategyConfig config = BsStrategyConfigDefault();
    char response[2048];
    const char* request =
        "POST /move HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 10\r\n"
        "\r\n"
        "{}";

    assert(BsArenaInit(&arena, 65536));
    BsHttpResult result = BsHandleHttpRequest(request, strlen(request), &arena, &config, response, sizeof(response));
    assert(result.status_code == 400);
    assert(strstr(response, "HTTP/1.1 400 Bad Request") != 0);
    BsArenaFree(&arena);
}

static size_t header_length(const char* request) {
    const char* terminator = strstr(request, "\r\n\r\n");
    assert(terminator != 0);
    return (size_t)(terminator - request) + 4u;
}

static void test_frame_incomplete_without_terminator(void) {
    const char* partial = "POST /move HTTP/1.1\r\nHost: x\r\n";
    size_t total = 12345u;
    assert(BsHttpRequestFrameLength(partial, strlen(partial), &total) == BS_HTTP_FRAME_INCOMPLETE);
    assert(total == 12345u);
}

static void test_frame_get_without_body(void) {
    const char* request = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";
    size_t total = 0;
    assert(BsHttpRequestFrameLength(request, strlen(request), &total) == BS_HTTP_FRAME_COMPLETE);
    assert(total == strlen(request));
    assert(total == header_length(request));
}

static void test_frame_post_reports_full_length(void) {
    char request[4096];
    request_from_body_with_headers(request, sizeof(request), "POST", "/move", "Content-Length", 0, MOVE_BODY);
    size_t total = 0;
    assert(BsHttpRequestFrameLength(request, strlen(request), &total) == BS_HTTP_FRAME_COMPLETE);
    assert(total == strlen(request));
    assert(total == header_length(request) + strlen(MOVE_BODY));
}

static void test_frame_reports_length_beyond_received_bytes(void) {
    const char* request =
        "POST /move HTTP/1.1\r\n"
        "Host: x\r\n"
        "content-length: 10\r\n"
        "\r\n"
        "{}";
    size_t total = 0;
    assert(BsHttpRequestFrameLength(request, strlen(request), &total) == BS_HTTP_FRAME_COMPLETE);
    assert(total == header_length(request) + 10u);
    assert(total > strlen(request));
}

static void test_frame_overflow_content_length_clamps(void) {
    const char* request =
        "POST /move HTTP/1.1\r\n"
        "Host: x\r\n"
        "Content-Length: 18446744073709551615\r\n"
        "\r\n";
    size_t total = 0;
    assert(BsHttpRequestFrameLength(request, strlen(request), &total) == BS_HTTP_FRAME_COMPLETE);
    assert(total == SIZE_MAX);
}

int main(void) {
    test_frame_incomplete_without_terminator();
    test_frame_get_without_body();
    test_frame_post_reports_full_length();
    test_frame_reports_length_beyond_received_bytes();
    test_frame_overflow_content_length_clamps();
    test_info_route();
    test_move_route();
    test_unknown_route();
    test_post_without_content_length();
    test_wrong_method_returns_405();
    test_lowercase_content_length_is_accepted();
    test_chunked_transfer_returns_501();
    test_strategy_error_returns_500();
    test_malformed_json_returns_400();
    test_missing_required_json_returns_400();
    test_post_with_appended_second_request_returns_400();
    test_get_with_stray_bytes_returns_400();
    test_content_length_just_over_limit_returns_413();
    test_tiny_response_capacity_returns_500_without_overflow();
    test_duplicate_content_length_handling();
    test_malformed_header_syntax_returns_400();
    test_short_body_mismatch_returns_400();
    return 0;
}
