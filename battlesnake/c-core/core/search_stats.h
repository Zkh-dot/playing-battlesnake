#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "../datatypes/board.h"

#define CORE_SEARCH_MAX_DEPTH 32

typedef enum {
    CORE_SEARCH_PARALLEL_SERIAL = 0,
    CORE_SEARCH_PARALLEL_ROOT_MOVES = 1,
    CORE_SEARCH_PARALLEL_PV_ROOT_MOVES = 2,
    CORE_SEARCH_PARALLEL_ROOT_REPLIES = 3,
    CORE_SEARCH_PARALLEL_PLY1_TASKS = 4,
    CORE_SEARCH_PARALLEL_LEAF_EVAL = 5,
} CoreSearchParallelMode;

typedef struct {
    uint64_t nodes;
    uint64_t leaf_evals;
    uint64_t clone_calls;
    uint64_t board_allocations;
    uint64_t safe_move_calls;
    uint64_t beta_cutoffs;
    uint64_t move_order_first_choice_cutoffs;
    uint64_t tt_probes;
    uint64_t tt_hits;
    uint64_t tt_exact_hits;
    uint64_t tt_lower_hits;
    uint64_t tt_upper_hits;
    uint64_t tt_cutoffs;
    uint64_t tt_stores;
    uint64_t tt_collisions;
    int completed_depth;
    int max_depth_started;
    bool timed_out;
    double score;
    double elapsed_ms;
    int parallel_mode;
    int parallel_workers_used;
    MoveDirection move;
    bool root_move_score_valid[4];
    double root_move_scores[4];
} CoreSearchStats;

typedef struct {
    double terminal_win;
    double terminal_loss;
    double base;
    double health;
    double length;
    double reachable_space;
    double safe_moves;
    double center;
    double food;
    double low_health_food;
    double low_health_threshold;
    double hazard_damage;
    double hazard;
    double length_advantage;
    double adjacent_equal_or_longer_penalty;
    double adjacent_shorter_bonus;
    double opponent_reachable_space;
    double territory_delta;
    double opponent_safe_moves;
    double opponent_low_health_food_denial;
} CoreEvaluationWeights;

typedef struct {
    int time_budget_ms;
    int fixed_depth;
    bool enable_tt;
    bool enable_move_ordering;
    bool enable_make_unmake;
    CoreSearchParallelMode parallel_mode;
    CoreEvaluationWeights weights;
} CoreSearchConfig;

CoreEvaluationWeights CoreEvaluationWeightsDefault(void);

CoreSearchConfig CoreSearchConfigDefault(int time_budget_ms);

void CoreSearchStatsInit(CoreSearchStats* stats);
