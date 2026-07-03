#include "search_stats.h"

#include <string.h>

CoreSearchConfig CoreSearchConfigDefault(int time_budget_ms) {
    CoreSearchConfig config;
    config.time_budget_ms = time_budget_ms;
    config.fixed_depth = 0;
    config.enable_tt = true;
    config.enable_move_ordering = true;
    config.enable_make_unmake = true;
    return config;
}

void CoreSearchStatsInit(CoreSearchStats* stats) {
    if (stats == NULL) {
        return;
    }

    memset(stats, 0, sizeof(*stats));
    stats->move = MOVE_INVALID;
}
