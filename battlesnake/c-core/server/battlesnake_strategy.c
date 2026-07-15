#include "battlesnake_strategy.h"

#include "../core/core_algorithms.h"

#include <stdint.h>
#include <string.h>

BsStrategyConfig BsStrategyConfigDefault(void) {
    BsStrategyConfig config;
    config.default_time_budget_ms = 400;
    config.game_timeout_ms = 0;
    config.safety_margin_ms = 150;
    config.min_time_budget_ms = 50;
    config.weight_profile = CoreDuelWeightProfileDefault();
    return config;
}

static bool is_legal_move(MoveDirection move) {
    return move == MOVE_UP || move == MOVE_DOWN || move == MOVE_LEFT || move == MOVE_RIGHT;
}

BsStrategyStatus BsChooseFallbackMove(
    const Board* board,
    const char* snake_id,
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

    MoveDirection moves[4];
    int count = BoardSafeMoves(board, snake_id, moves);
    if (count > 0) {
        *out_move = moves[0];
        return BS_STRATEGY_FALLBACK_USED;
    }
    if (board->snake_count == 2) {
        CoreDuelRootProfileResult profile;
        if (CoreDuelRootProfile(board, snake_id, &profile) != CORE_OK) {
            return BS_STRATEGY_ERROR;
        }
        for (int move = MOVE_UP; move <= MOVE_RIGHT; move++) {
            if (profile.commands[move].alive_reply_count > 0) {
                *out_move = (MoveDirection)move;
                return BS_STRATEGY_FALLBACK_USED;
            }
        }
        for (int move = MOVE_UP; move <= MOVE_RIGHT; move++) {
            const CoreDuelRootCommandProfile* command = &profile.commands[move];
            if (command->opponent_reply_mask != 0 &&
                command->draw_reply_mask == command->opponent_reply_mask) {
                *out_move = (MoveDirection)move;
                return BS_STRATEGY_FALLBACK_USED;
            }
        }
    }
    *out_move = MOVE_UP;
    return BS_STRATEGY_FALLBACK_USED;
}

bool BsStrategyHasSearchWindow(const BsStrategyConfig* config, int elapsed_ms) {
    BsStrategyConfig effective = BsStrategyConfigDefault();
    if (config != 0) {
        effective = *config;
    }

    int64_t remaining_ms = (int64_t)effective.game_timeout_ms - (int64_t)elapsed_ms;
    remaining_ms -= (int64_t)effective.safety_margin_ms;
    return remaining_ms >= (int64_t)effective.min_time_budget_ms;
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

bool BsStrategyDuelSearchConfig(
    const BsStrategyConfig* config,
    CoreSearchConfig* out_config
) {
    if (out_config == NULL) {
        return false;
    }
    const CoreDuelWeightProfile* weight_profile = config != NULL
        ? config->weight_profile
        : NULL;
    if (weight_profile == NULL) {
        weight_profile = CoreDuelWeightProfileDefault();
    }
    if (weight_profile == NULL) {
        return false;
    }

    *out_config = CoreSearchConfigDefault(BsStrategyEffectiveBudgetMs(config));
    out_config->weights = weight_profile->weights;
    return true;
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

    CoreSearchConfig search_config;
    if (!BsStrategyDuelSearchConfig(config, &search_config)) {
        return BS_STRATEGY_ERROR;
    }

    /* Minimax search is applied to 1v1 duels. Battlesnake ladder duels are
     * delivered as standard ruleset games with two snakes, while local duel
     * tooling may use solo. */
    if (board->ruleset_name != 0 &&
        (strcmp(board->ruleset_name, "solo") == 0 || strcmp(board->ruleset_name, "standard") == 0) &&
        board->snake_count == 2) {
        CoreSearchStats search_stats;
        CoreStatus status = CoreMinimaxMoveWithStats(
            board,
            snake_id,
            search_config,
            out_move,
            &search_stats
        );
        if (status == CORE_ERROR) {
            return BS_STRATEGY_ERROR;
        }
        if (status == CORE_OK && is_legal_move(*out_move)) {
            return BS_STRATEGY_OK;
        }
    }

    return BsChooseFallbackMove(board, snake_id, out_move);
}
