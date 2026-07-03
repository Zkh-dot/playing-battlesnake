#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "position_eval.h"

#include <string.h>

CorePositionEvalConfig CorePositionEvalConfigDefault(int time_budget_ms) {
    CorePositionEvalConfig config = {0};
    config.time_budget_ms = time_budget_ms < 1 ? 1 : time_budget_ms;
    config.weights = CoreEvaluationWeightsDefault();
    config.decision_mode = CORE_POSITION_DECISION_MATRIX;
    return config;
}

CoreStatus CorePositionEvaluateDuel(
    const Board* board,
    const char* first_snake_id,
    const char* second_snake_id,
    CorePositionEvalConfig config,
    CorePositionEvalResult* out_result
) {
    if (out_result == NULL) {
        return CORE_ERROR;
    }
    memset(out_result, 0, sizeof(*out_result));
    (void)board;
    (void)first_snake_id;
    (void)second_snake_id;
    (void)config;
    return CORE_NOT_IMPLEMENTED;
}
