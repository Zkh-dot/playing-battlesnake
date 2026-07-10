#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../datatypes/board.h"
#include "search_value.h"

typedef CoreValueBound CoreTtBound;

#define CORE_TT_EXACT CORE_VALUE_BOUND_EXACT
#define CORE_TT_LOWER CORE_VALUE_BOUND_LOWER
#define CORE_TT_UPPER CORE_VALUE_BOUND_UPPER

typedef enum {
    CORE_TT_MISS = 0,
    CORE_TT_HIT = 1,
    CORE_TT_CUTOFF = 2,
} CoreTtProbeResult;

typedef struct {
    uint64_t hash;
    CoreSearchValue value;
    int depth;
    int generation;
    CoreTtBound bound;
    MoveDirection best_move;
    bool occupied;
} CoreTtEntry;

typedef struct {
    CoreTtEntry* entries;
    size_t capacity;
    int generation;
} CoreTranspositionTable;

bool CoreTtInit(CoreTranspositionTable* table, size_t capacity);
void CoreTtFree(CoreTranspositionTable* table);
void CoreTtNextGeneration(CoreTranspositionTable* table);
CoreTtProbeResult CoreTtProbe(
    const CoreTranspositionTable* table,
    uint64_t hash,
    int depth,
    double alpha,
    double beta,
    CoreSearchValue* out_value,
    MoveDirection* out_best_move,
    CoreTtBound* out_bound,
    bool* out_collision
);
bool CoreTtStore(
    CoreTranspositionTable* table,
    uint64_t hash,
    int depth,
    CoreSearchValue value,
    CoreTtBound bound,
    MoveDirection best_move,
    bool* out_collision
);
