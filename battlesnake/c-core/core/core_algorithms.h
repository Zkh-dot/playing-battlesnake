#pragma once

#include <stdbool.h>

#include "../datatypes/board.h"
#include "search_stats.h"

typedef enum {
    CORE_OK = 0,
    CORE_NOT_IMPLEMENTED = 1,
    CORE_ERROR = 2,
} CoreStatus;

typedef struct {
    char* snake_id;
    Coord* coords;
    int coord_count;
} CoreTerritoryEntry;

typedef struct {
    CoreTerritoryEntry* entries;
    int entry_count;
} CoreTerritory;

const char* CoreNotImplementedMessage(void);

void CoreTerritoryFree(CoreTerritory* territory);

CoreStatus CoreReachableSpace(const Board* board, Coord start, const char* snake_id, int* out_count);

CoreStatus CoreShortestPath(
    const Board* board,
    Coord start,
    Coord goal,
    const char* snake_id,
    Coord** out_path,
    int* out_path_count
);

CoreStatus CoreVoronoiTerritory(const Board* board, void** out_territory);

CoreStatus CoreMinimaxMove(
    const Board* board,
    const char* snake_id,
    int time_budget_ms,
    MoveDirection* out_move
);

CoreStatus CoreMinimaxMoveWithStats(
    const Board* board,
    const char* snake_id,
    CoreSearchConfig config,
    MoveDirection* out_move,
    CoreSearchStats* out_stats
);

CoreStatus CoreChokePoints(
    const Board* board,
    const char* snake_id,
    Coord** out_points,
    int* out_points_count
);

CoreStatus CoreEdgeTrapMove(
    const Board* board,
    const char* snake_id,
    bool* out_has_move,
    MoveDirection* out_move
);

CoreStatus CorePredictHazards(
    const Board* board,
    int turns_ahead,
    Coord** out_hazards,
    int* out_hazard_count
);

CoreStatus CoreEvaluate(const Board* board, const char* snake_id, double* out_score);
