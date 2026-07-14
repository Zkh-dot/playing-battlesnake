#pragma once

#include "arena.h"
#include "battlesnake_strategy.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    /* Nonnegative monotonic duration; negative values are clamped to zero. */
    int elapsed_before_handle_ms;
} BsHttpRequestContext;

typedef struct {
    int status_code;
    size_t response_len;
    bool is_move;
    bool fallback_used;
    int game_timeout_ms;
    int elapsed_before_search_ms;
} BsHttpResult;

typedef enum {
    /* The header block has not been fully received yet; read more bytes. */
    BS_HTTP_FRAME_INCOMPLETE = 0,
    /* Headers are complete; *out_total_len holds the full request length. */
    BS_HTTP_FRAME_COMPLETE = 1,
} BsHttpFrameStatus;

/*
 * Determine how many bytes make up a complete HTTP request from a partial
 * buffer, so a socket reader knows when to stop reading. Returns
 * BS_HTTP_FRAME_INCOMPLETE until the CRLFCRLF header terminator is present.
 * On BS_HTTP_FRAME_COMPLETE, *out_total_len is the header length plus the
 * declared Content-Length body (or just the header length when no valid
 * Content-Length is present); it is clamped to SIZE_MAX if the sum would
 * overflow. Full validation is still performed by BsHandleHttpRequest.
 */
BsHttpFrameStatus BsHttpRequestFrameLength(
    const char* data,
    size_t len,
    size_t* out_total_len
);

BsHttpResult BsHandleHttpRequest(
    const char* request,
    size_t request_len,
    BsArena* arena,
    const BsStrategyConfig* strategy_config,
    char* response,
    size_t response_capacity
);

BsHttpResult BsHandleHttpRequestTimed(
    const char* request,
    size_t request_len,
    BsArena* arena,
    const BsStrategyConfig* strategy_config,
    const BsHttpRequestContext* request_context,
    char* response,
    size_t response_capacity
);
