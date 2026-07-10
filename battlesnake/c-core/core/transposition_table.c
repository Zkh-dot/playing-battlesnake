#include "transposition_table.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static size_t core_tt_index(const CoreTranspositionTable* table, uint64_t hash) {
    return (size_t)(hash % table->capacity);
}

bool CoreTtInit(CoreTranspositionTable* table, size_t capacity) {
    if (table == NULL || capacity == 0) {
        return false;
    }

    table->entries = (CoreTtEntry*)calloc(capacity, sizeof(CoreTtEntry));
    if (table->entries == NULL) {
        table->capacity = 0;
        table->generation = 0;
        return false;
    }

    table->capacity = capacity;
    table->generation = 1;
    return true;
}

void CoreTtFree(CoreTranspositionTable* table) {
    if (table == NULL) {
        return;
    }

    free(table->entries);
    table->entries = NULL;
    table->capacity = 0;
    table->generation = 0;
}

void CoreTtNextGeneration(CoreTranspositionTable* table) {
    if (table == NULL || table->entries == NULL || table->capacity == 0) {
        return;
    }

    if (table->generation == INT_MAX) {
        memset(table->entries, 0, table->capacity * sizeof(CoreTtEntry));
        table->generation = 1;
        return;
    }

    table->generation++;
}

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
) {
    if (out_collision != NULL) {
        *out_collision = false;
    }
    if (table == NULL || table->entries == NULL || table->capacity == 0) {
        return CORE_TT_MISS;
    }

    const CoreTtEntry* entry = &table->entries[core_tt_index(table, hash)];
    if (!entry->occupied) {
        return CORE_TT_MISS;
    }
    if (entry->hash != hash) {
        if (out_collision != NULL) {
            *out_collision = true;
        }
        return CORE_TT_MISS;
    }

    if (out_best_move != NULL) {
        *out_best_move = entry->best_move;
    }
    if (out_bound != NULL) {
        *out_bound = entry->bound;
    }

    if (entry->depth < depth) {
        return CORE_TT_HIT;
    }

    if (entry->bound == CORE_TT_EXACT ||
        (entry->bound == CORE_TT_LOWER && entry->value.score >= beta) ||
        (entry->bound == CORE_TT_UPPER && entry->value.score <= alpha)) {
        if (out_value != NULL) {
            *out_value = entry->value;
        }
        return CORE_TT_CUTOFF;
    }

    return CORE_TT_HIT;
}

bool CoreTtStore(
    CoreTranspositionTable* table,
    uint64_t hash,
    int depth,
    CoreSearchValue value,
    CoreTtBound bound,
    MoveDirection best_move,
    bool* out_collision
) {
    if (out_collision != NULL) {
        *out_collision = false;
    }
    if (table == NULL || table->entries == NULL || table->capacity == 0) {
        return false;
    }

    CoreTtEntry* entry = &table->entries[core_tt_index(table, hash)];
    if (entry->occupied && entry->hash != hash) {
        if (out_collision != NULL) {
            *out_collision = true;
        }
    }
    if (entry->occupied && entry->generation == table->generation && entry->depth > depth) {
        return false;
    }

    entry->hash = hash;
    value.bound = bound;
    entry->value = value;
    entry->depth = depth;
    entry->generation = table->generation;
    entry->bound = bound;
    entry->best_move = best_move;
    entry->occupied = true;
    return true;
}
