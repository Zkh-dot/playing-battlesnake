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
    CORE_STRUCTURAL_PROOF_NOT_ANALYZED = 0,
    CORE_STRUCTURAL_PROOF_SAFE = 1,
    CORE_STRUCTURAL_PROOF_UNSAFE = 2,
    CORE_STRUCTURAL_PROOF_UNKNOWN = 3,
} CoreStructuralProofResult;

typedef enum {
    CORE_STRUCTURAL_CUTOFF_NONE = 0,
    CORE_STRUCTURAL_CUTOFF_CAPACITY = 1,
    CORE_STRUCTURAL_CUTOFF_CYCLE = 2,
    CORE_STRUCTURAL_CUTOFF_HORIZON = 3,
    CORE_STRUCTURAL_CUTOFF_DEAD_END = 4,
    CORE_STRUCTURAL_CUTOFF_DEADLINE = 5,
    CORE_STRUCTURAL_CUTOFF_RESOURCE_LIMIT = 6,
    CORE_STRUCTURAL_CUTOFF_ALLOCATION_FAILURE = 7,
    CORE_STRUCTURAL_CUTOFF_POLICY_SUFFICIENT = 8,
    CORE_STRUCTURAL_CUTOFF_SURVIVABILITY = 9,
    CORE_STRUCTURAL_CUTOFF_BOUNDED_LASSO = 10,
} CoreStructuralProofCutoff;

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
    CORE_ROOT_REJECTION_STRUCTURALLY_DOMINATED = 3,
} CoreRootRejectionReason;

typedef enum {
    CORE_SELECTION_MINIMAX = 0,
    CORE_SELECTION_TIMEOUT_BEST_SO_FAR = 1,
    CORE_SELECTION_ALLOWED_FALLBACK = 2,
    CORE_SELECTION_CORRIDOR_GUARD = 3,
} CoreSelectionReason;

typedef enum {
    CORE_ROOT_COMPARISON_NOT_COMPARED = 0,
    CORE_ROOT_COMPARISON_TERMINAL_OUTCOME = 1,
    CORE_ROOT_COMPARISON_SEARCH_BOUND = 2,
    CORE_ROOT_COMPARISON_STRUCTURAL_PROOF = 3,
    CORE_ROOT_COMPARISON_TERMINAL_SURVIVAL = 4,
    CORE_ROOT_COMPARISON_HEURISTIC_VALUE = 5,
    CORE_ROOT_COMPARISON_STRUCTURAL_TIEBREAK = 6,
    CORE_ROOT_COMPARISON_PREVIOUS_PV = 7,
    CORE_ROOT_COMPARISON_STABLE_DIRECTION = 8,
    CORE_ROOT_COMPARISON_CORRIDOR_GUARD = 9,
    CORE_ROOT_COMPARISON_NUMERIC_VALUE = 10,
} CoreRootComparisonReason;

typedef enum {
    CORE_ROOT_COMPARISON_INCUMBENT = -1,
    CORE_ROOT_COMPARISON_EQUAL = 0,
    CORE_ROOT_COMPARISON_CANDIDATE = 1,
    CORE_ROOT_COMPARISON_INCOMPARABLE = 2,
} CoreRootComparisonOrdering;

/* INCOMPARABLE is a semantic result, not a tie. This pairwise comparator only
 * applies lower layers when both search records are exact with the same
 * outcome. Task 2 root selection must retain a global maximal candidate set
 * (or use an equivalent layered treatment) before structural tie-breaks,
 * previous-PV preference, or stable direction. */
typedef struct {
    CoreRootComparisonOrdering ordering;
    CoreRootComparisonReason reason;
} CoreRootComparison;

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
    CoreStructuralProofResult structural_proof;
    CoreStructuralProofCutoff proof_cutoff;
    int proof_horizon;
    uint64_t explored_states;
    int structural_capacity;
    bool opponent_closure_considered;
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
    CoreRootComparisonReason root_comparison_reason;
    uint64_t root_analysis_nodes;
    double root_analysis_elapsed_ms;
    int root_analysis_budget_ms;
    /* Scheduled interval after the structural prefix; it does not guarantee
     * that a noninterruptible search leaf completes a depth. */
    int search_reserved_ms;
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
