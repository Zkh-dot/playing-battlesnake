#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "../datatypes/board.h"
#include "core_algorithms.h"
#include "search_stats.h"

typedef enum {
    CORE_POSITION_DECISION_MATRIX = 0,
    CORE_POSITION_DECISION_PURE_MINIMAX = 1,
} CorePositionDecisionMode;

typedef struct {
    int time_budget_ms;
    int max_depth;
    CoreEvaluationWeights weights;
    CorePositionDecisionMode decision_mode;
} CorePositionEvalConfig;

typedef struct {
    double first_win_probability;
    double confidence;
    double first_move_probabilities[4];  /* indexed by MoveDirection */
    double second_move_probabilities[4]; /* indexed by MoveDirection */
    uint64_t nodes;
    uint64_t terminal_leaves;
    uint64_t heuristic_leaves;
    uint64_t timeout_leaves;
    uint64_t expanded_children;
    bool timed_out;
    double elapsed_ms;
} CorePositionEvalResult;

CorePositionEvalConfig CorePositionEvalConfigDefault(int time_budget_ms);

CoreStatus CorePositionEvaluateDuel(
    const Board* board,
    const char* first_snake_id,
    const char* second_snake_id,
    CorePositionEvalConfig config,
    CorePositionEvalResult* out_result
);
