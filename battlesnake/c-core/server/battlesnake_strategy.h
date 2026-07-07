#pragma once

#include "../datatypes/board.h"

typedef struct {
    int default_time_budget_ms;
    int game_timeout_ms;
    int safety_margin_ms;
    int min_time_budget_ms;
} BsStrategyConfig;

typedef enum {
    BS_STRATEGY_OK = 0,
    BS_STRATEGY_FALLBACK_USED = 1,
    BS_STRATEGY_ERROR = 2,
} BsStrategyStatus;

BsStrategyConfig BsStrategyConfigDefault(void);
int BsStrategyEffectiveBudgetMs(const BsStrategyConfig* config);
BsStrategyStatus BsChooseMove(
    const Board* board,
    const char* snake_id,
    const BsStrategyConfig* config,
    MoveDirection* out_move
);
