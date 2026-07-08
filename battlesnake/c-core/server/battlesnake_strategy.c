#include "battlesnake_strategy.h"

#include "../core/core_algorithms.h"

#include <string.h>

BsStrategyConfig BsStrategyConfigDefault(void) {
    BsStrategyConfig config;
    config.default_time_budget_ms = 400;
    config.game_timeout_ms = 0;
    config.safety_margin_ms = 150;
    config.min_time_budget_ms = 50;
    return config;
}

static bool is_legal_move(MoveDirection move) {
    return move == MOVE_UP || move == MOVE_DOWN || move == MOVE_LEFT || move == MOVE_RIGHT;
}

static BsStrategyStatus fallback_move(const Board* board, const char* snake_id, MoveDirection* out_move) {
    MoveDirection moves[4];
    int count = BoardSafeMoves(board, snake_id, moves);
    if (count > 0) {
        *out_move = moves[0];
    } else {
        *out_move = MOVE_UP;
    }
    return BS_STRATEGY_FALLBACK_USED;
}

int BsStrategyEffectiveBudgetMs(const BsStrategyConfig* config) {
    BsStrategyConfig effective = BsStrategyConfigDefault();
    if (config != 0) {
        effective = *config;
    }

    int budget = effective.default_time_budget_ms > 0 ? effective.default_time_budget_ms : 1;
    if (effective.game_timeout_ms > 0) {
        int min_budget = effective.min_time_budget_ms > 0 ? effective.min_time_budget_ms : 1;
        int safety_margin = effective.safety_margin_ms > 0 ? effective.safety_margin_ms : 0;
        int deadline_budget = effective.game_timeout_ms - safety_margin;
        if (deadline_budget < min_budget) {
            deadline_budget = min_budget;
        }
        if (budget > deadline_budget) {
            budget = deadline_budget;
        }
    }
    return budget > 0 ? budget : 1;
}

BsStrategyStatus BsChooseMove(
    const Board* board,
    const char* snake_id,
    const BsStrategyConfig* config,
    MoveDirection* out_move
) {
    if (out_move != 0) {
        *out_move = MOVE_INVALID;
    }
    if (board == 0 || snake_id == 0 || out_move == 0) {
        return BS_STRATEGY_ERROR;
    }
    if (BoardFindSnakeConst(board, snake_id) == 0) {
        return BS_STRATEGY_ERROR;
    }

    int budget = BsStrategyEffectiveBudgetMs(config);

    /* Minimax search is applied to 1v1 duels. Battlesnake ladder duels are
     * delivered as standard ruleset games with two snakes, while local duel
     * tooling may use solo. */
    if (board->ruleset_name != 0 &&
        (strcmp(board->ruleset_name, "solo") == 0 || strcmp(board->ruleset_name, "standard") == 0) &&
        board->snake_count == 2) {
        CoreStatus status = CoreMinimaxMove(board, snake_id, budget, out_move);
        if (status == CORE_ERROR) {
            return BS_STRATEGY_ERROR;
        }
        if (status == CORE_OK && is_legal_move(*out_move)) {
            return BS_STRATEGY_OK;
        }
    }

    return fallback_move(board, snake_id, out_move);
}
