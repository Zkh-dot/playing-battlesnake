#pragma once

#include "../datatypes/board.h"

typedef struct {
    int default_time_budget_ms;
} BsStrategyConfig;

typedef enum {
    BS_STRATEGY_OK = 0,
    BS_STRATEGY_FALLBACK_USED = 1,
    BS_STRATEGY_ERROR = 2,
} BsStrategyStatus;

BsStrategyConfig BsStrategyConfigDefault(void);
BsStrategyStatus BsChooseMove(
    const Board* board,
    const char* snake_id,
    const BsStrategyConfig* config,
    MoveDirection* out_move
);
