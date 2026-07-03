#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../datatypes/board.h"

typedef enum {
    CORE_TT_EXACT = 0,
    CORE_TT_LOWER = 1,
    CORE_TT_UPPER = 2,
} CoreTtBound;

typedef enum {
    CORE_TT_MISS = 0,
    CORE_TT_HIT = 1,
    CORE_TT_CUTOFF = 2,
} CoreTtProbeResult;

typedef struct {
    uint64_t hash;
    double score;
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
    double* out_score,
    MoveDirection* out_best_move,
    CoreTtBound* out_bound,
    bool* out_collision
);
bool CoreTtStore(
    CoreTranspositionTable* table,
    uint64_t hash,
    int depth,
    double score,
    CoreTtBound bound,
    MoveDirection best_move,
    bool* out_collision
);
