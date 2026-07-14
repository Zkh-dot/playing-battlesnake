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

typedef struct {
    int reachable_cells;
    int max_arrival;
    bool tail_reachable;
    bool dead;
} CoreSpaceTimeMetrics;

CoreStatus CoreSpaceTimeCompute(
    const Board* board,
    const char* snake_id,
    CoreSpaceTimeMetrics* out_metrics
);

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

CoreRootComparison CoreCompareRootCandidates(
    const CoreSearchValue* candidate_value,
    const CoreRootCandidateStats* candidate_stats,
    const CoreSearchValue* incumbent_value,
    const CoreRootCandidateStats* incumbent_stats
);

CoreOutcome CoreClassifyDuelOutcome(
    const Board* board,
    const char* snake_id,
    const char* opponent_id
);

typedef struct {
    bool evaluated;
    bool safe_by_board_rules;
    uint8_t opponent_reply_mask;
    uint8_t win_reply_mask;
    uint8_t draw_reply_mask;
    uint8_t both_alive_reply_mask;
    uint8_t loss_reply_mask;
    uint8_t alive_reply_mask;
    int alive_reply_count;
    uint32_t immediate_causes;
} CoreDuelRootCommandProfile;

typedef struct {
    CoreDuelRootCommandProfile commands[4];
    uint8_t opponent_command_mask;
} CoreDuelRootProfileResult;

CoreStatus CoreDuelRootProfile(
    const Board* board,
    const char* snake_id,
    CoreDuelRootProfileResult* out_result
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

CoreStatus CoreEvaluateWithWeights(
    const Board* board,
    const char* snake_id,
    const CoreEvaluationWeights* weights,
    double* out_score
);

CoreStatus CoreEvaluate(const Board* board, const char* snake_id, double* out_score);
