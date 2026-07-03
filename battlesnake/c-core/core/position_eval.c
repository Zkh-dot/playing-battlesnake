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
    if (board == NULL || first_snake_id == NULL || second_snake_id == NULL || out_result == NULL) {
        return CORE_ERROR;
    }

    memset(out_result, 0, sizeof(*out_result));

    out_result->nodes = 1;
    (void)config;

    const Snake* first_snake = BoardFindSnakeConst(board, first_snake_id);
    const Snake* second_snake = BoardFindSnakeConst(board, second_snake_id);

    bool first_alive = first_snake != NULL && first_snake->body_len > 0;
    bool second_alive = second_snake != NULL && second_snake->body_len > 0;

    if (first_alive && !second_alive) {
        out_result->first_win_probability = 1.0;
        out_result->confidence = 1.0;
        out_result->terminal_leaves = 1;
        return CORE_OK;
    }

    if (!first_alive && second_alive) {
        out_result->first_win_probability = 0.0;
        out_result->confidence = 1.0;
        out_result->terminal_leaves = 1;
        return CORE_OK;
    }

    if (!first_alive && !second_alive) {
        out_result->first_win_probability = 0.5;
        out_result->confidence = 1.0;
        out_result->terminal_leaves = 1;
        return CORE_OK;
    }

    out_result->first_win_probability = 0.5;
    out_result->confidence = 0.0;
    out_result->heuristic_leaves = 1;
    return CORE_OK;
}
