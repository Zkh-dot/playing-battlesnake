#include "battlesnake_strategy.h"

#include "../core/core_algorithms.h"

#include <string.h>

BsStrategyConfig BsStrategyConfigDefault(void) {
    BsStrategyConfig config;
    config.default_time_budget_ms = 400;
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

    int budget = BsStrategyConfigDefault().default_time_budget_ms;
    if (config != 0) {
        budget = config->default_time_budget_ms;
    }

    /* Minimax search is applied only to solo-ruleset 1v1 duels, the production
     * runtime target for this native server. Every other ruleset (royale,
     * constrictor, standard, 4+ snakes) intentionally uses the safe fallback
     * move; the FastAPI comparator retains the per-ruleset strategies. */
    if (board->ruleset_name != 0 && strcmp(board->ruleset_name, "solo") == 0 && board->snake_count == 2) {
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
