#include "search_stats.h"

#include <string.h>

CoreEvaluationWeights CoreEvaluationWeightsDefault(void) {
    CoreEvaluationWeights weights;
    weights.terminal_win = 1000000.0;
    weights.terminal_loss = -1000000.0;
    weights.base = 500.0;
    weights.health = 0.7;
    weights.length = 18.0;
    weights.reachable_space = 4.0;
    weights.safe_moves = 35.0;
    weights.center = 2.0;
    weights.food = 55.0;
    weights.low_health_food = 120.0;
    weights.low_health_threshold = 35.0;
    weights.hazard_damage = 1.0;
    weights.hazard = 25.0;
    weights.length_advantage = 5.0;
    weights.adjacent_equal_or_longer_penalty = 120.0;
    weights.adjacent_shorter_bonus = 45.0;
    weights.opponent_reachable_space = 0.0;
    weights.territory_delta = 0.0;
    weights.opponent_safe_moves = 0.0;
    weights.opponent_low_health_food_denial = 0.0;
    return weights;
}

CoreSearchConfig CoreSearchConfigDefault(int time_budget_ms) {
    CoreSearchConfig config;
    config.time_budget_ms = time_budget_ms;
    config.fixed_depth = 0;
    config.enable_tt = true;
    config.enable_move_ordering = true;
    config.enable_make_unmake = true;
    config.parallel_mode = CORE_SEARCH_PARALLEL_SERIAL;
    config.weights = CoreEvaluationWeightsDefault();
    return config;
}

void CoreSearchStatsInit(CoreSearchStats* stats) {
    if (stats == NULL) {
        return;
    }

    memset(stats, 0, sizeof(*stats));
    stats->parallel_mode = CORE_SEARCH_PARALLEL_SERIAL;
    stats->parallel_workers_used = 1;
    stats->move = MOVE_INVALID;
}
