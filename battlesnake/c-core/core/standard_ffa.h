#pragma once

#include "core_algorithms.h"

typedef struct {
    CoreEvaluationWeights evaluation;
    double w_expected;
    double w_worst;
    double w_space_log;
    double w_space_ratio;
    double w_escape;
    double w_zero_escape;
    double w_losing_h2h;
    double w_winning_h2h;
    double w_food_on_cell;
    double w_food_route;
    double w_contested_food;
    double w_pocket;
    double food_urgency_health;
    double pocket_space_per_length;
    double nearby_opponent_distance;
    double deepening_enabled;
    double deepening_depth;
    double deepening_top_candidates;
    double deepening_interaction_radius;
    double deepening_trap_penalty;
    int max_scenarios;
    int time_budget_ms;
} CoreStandardFfaConfig;

CoreStandardFfaConfig CoreStandardFfaConfigDefault(int time_budget_ms);

CoreStatus CoreStandardFfaMove(
    const Board* board,
    const char* snake_id,
    const CoreStandardFfaConfig* config,
    MoveDirection* out_move
);
