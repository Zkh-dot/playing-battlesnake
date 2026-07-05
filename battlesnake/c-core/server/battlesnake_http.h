#pragma once

#include "arena.h"
#include "battlesnake_strategy.h"

#include <stddef.h>

typedef struct {
    int status_code;
    size_t response_len;
} BsHttpResult;

BsHttpResult BsHandleHttpRequest(
    const char* request,
    size_t request_len,
    BsArena* arena,
    const BsStrategyConfig* strategy_config,
    char* response,
    size_t response_capacity
);
