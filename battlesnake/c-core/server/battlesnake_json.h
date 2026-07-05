#pragma once

#include "../datatypes/board.h"
#include "arena.h"

#include <stddef.h>

typedef enum {
    BS_JSON_OK = 0,
    BS_JSON_MALFORMED = 1,
    BS_JSON_MISSING_REQUIRED = 2,
    BS_JSON_NO_MEMORY = 3,
} BsJsonStatus;

typedef struct {
    /* Arena-backed; valid until the arena is reset or freed. */
    char* game_id;
    int turn;
    /* Arena-backed; valid until the arena is reset or freed. */
    char* you_id;
    /* Heap-owned; release with BsGameRequestFree before arena reset/free. */
    Board* board;
    int timeout_ms;
} BsGameRequest;

BsJsonStatus BsParseGameRequest(
    const char* body,
    size_t body_len,
    BsArena* arena,
    BsGameRequest* out_request
);
void BsGameRequestFree(BsGameRequest* request);
const char* BsJsonStatusText(BsJsonStatus status);
