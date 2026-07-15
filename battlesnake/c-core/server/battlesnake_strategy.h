#pragma once

#include "../datatypes/board.h"
#include "../core/duel_weight_profiles_generated.h"

#include <stdbool.h>

typedef struct {
    int default_time_budget_ms;
    int game_timeout_ms;
    int safety_margin_ms;
    int min_time_budget_ms;
    const CoreDuelWeightProfile* weight_profile;
} BsStrategyConfig;

typedef enum {
    BS_STRATEGY_OK = 0,
    BS_STRATEGY_FALLBACK_USED = 1,
    BS_STRATEGY_ERROR = 2,
} BsStrategyStatus;

BsStrategyConfig BsStrategyConfigDefault(void);
bool BsStrategyDuelSearchConfig(
    const BsStrategyConfig* config,
    CoreSearchConfig* out_config
);
int BsStrategyEffectiveBudgetMs(const BsStrategyConfig* config);
bool BsStrategyHasSearchWindow(const BsStrategyConfig* config, int elapsed_ms);
BsStrategyStatus BsChooseFallbackMove(
    const Board* board,
    const char* snake_id,
    MoveDirection* out_move
);
BsStrategyStatus BsChooseMove(
    const Board* board,
    const char* snake_id,
    const BsStrategyConfig* config,
    MoveDirection* out_move
);
