#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "../datatypes/board.h"

#define CORE_SEARCH_MAX_DEPTH 32

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
    MoveDirection move;
} CoreSearchStats;

typedef struct {
    int time_budget_ms;
    int fixed_depth;
    bool enable_tt;
    bool enable_move_ordering;
    bool enable_make_unmake;
} CoreSearchConfig;

CoreSearchConfig CoreSearchConfigDefault(int time_budget_ms);

void CoreSearchStatsInit(CoreSearchStats* stats);
