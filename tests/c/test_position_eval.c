#include "../../battlesnake/c-core/core/position_eval.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static void test_default_config(void) {
    CorePositionEvalConfig config = CorePositionEvalConfigDefault(7000);

    assert(config.time_budget_ms == 7000);
    assert(config.max_depth == 0);
    assert(config.decision_mode == CORE_POSITION_DECISION_MATRIX);
    assert(config.weights.terminal_win == 1000000.0);
}

int main(void) {
    test_default_config();
    puts("position_eval C tests passed");
    return 0;
}
