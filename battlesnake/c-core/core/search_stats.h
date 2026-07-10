#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "../datatypes/board.h"
#include "search_value.h"

#define CORE_SEARCH_MAX_DEPTH 32

typedef enum {
    CORE_SEARCH_PARALLEL_SERIAL = 0,
    CORE_SEARCH_PARALLEL_ROOT_MOVES = 1,
    CORE_SEARCH_PARALLEL_PV_ROOT_MOVES = 2,
    CORE_SEARCH_PARALLEL_ROOT_REPLIES = 3,
    CORE_SEARCH_PARALLEL_PLY1_TASKS = 4,
    CORE_SEARCH_PARALLEL_LEAF_EVAL = 5,
} CoreSearchParallelMode;

typedef enum {
    CORE_ROOT_POLICY_STRICT_MINIMAX = 0,
    CORE_ROOT_POLICY_STANDARD_LADDER_OPPORTUNITY = 1,
} CoreRootPolicy;

typedef enum {
    CORE_TRAP_NOT_ANALYZED = 0,
    CORE_TRAP_IMMEDIATE_DEATH = 1,
    CORE_TRAP_PROVEN_SELF_TRAP = 2,
    CORE_TRAP_OPEN_BRANCH = 3,
    CORE_TRAP_SURVIVES_CYCLE = 4,
    CORE_TRAP_SURVIVES_HORIZON = 5,
    CORE_TRAP_UNKNOWN = 6,
} CoreTrapStatus;

typedef enum {
    CORE_REFUTATION_NOT_ANALYZED = 0,
    CORE_REFUTATION_PROVEN_REFUTABLE = 1,
    CORE_REFUTATION_NOT_REFUTABLE = 2,
    CORE_REFUTATION_UNKNOWN = 3,
} CoreRefutationStatus;

typedef enum {
    CORE_ROOT_REJECTION_NONE = 0,
    CORE_ROOT_REJECTION_NO_SURVIVING_REPLY = 1,
    CORE_ROOT_REJECTION_PROVEN_SHORT_SELF_TRAP = 2,
} CoreRootRejectionReason;

typedef enum {
    CORE_SELECTION_MINIMAX = 0,
    CORE_SELECTION_TIMEOUT_BEST_SO_FAR = 1,
    CORE_SELECTION_ALLOWED_FALLBACK = 2,
    CORE_SELECTION_CORRIDOR_GUARD = 3,
} CoreSelectionReason;

typedef struct {
    bool evaluated;
    bool allowed;
    bool safe_by_board_rules;
    CoreRootRejectionReason rejection_reason;
    uint8_t opponent_reply_mask;
    uint8_t win_reply_mask;
    uint8_t draw_reply_mask;
    uint8_t both_alive_reply_mask;
    uint8_t loss_reply_mask;
    uint8_t alive_reply_mask;
    int alive_reply_count;
    uint32_t immediate_causes;
    CoreTrapStatus trap_status;
    int trap_horizon;
    int post_move_length;
    int relaxed_static_capacity;
    CoreRefutationStatus refutation_status;
    bool minimax_value_valid;
    CoreSearchValue minimax_value;
} CoreRootCandidateStats;

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
    CoreSearchValue value;
    double elapsed_ms;
    int parallel_mode;
    int parallel_workers_used;
    MoveDirection move;
    bool root_move_score_valid[4];
    double root_move_scores[4];
    CoreRootCandidateStats root_candidates[4];
    uint8_t root_allowed_mask;
    CoreRootPolicy root_policy_applied;
    CoreSelectionReason selection_reason;
    uint64_t root_analysis_nodes;
    double root_analysis_elapsed_ms;
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
    CoreRootPolicy root_policy;
    CoreEvaluationWeights weights;
} CoreSearchConfig;

CoreEvaluationWeights CoreEvaluationWeightsDefault(void);

CoreSearchConfig CoreSearchConfigDefault(int time_budget_ms);

void CoreSearchStatsInit(CoreSearchStats* stats);
