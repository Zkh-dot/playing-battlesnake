#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "../datatypes/board.h"
#include "search_stats.h"

typedef struct {
    unsigned char* occupied;
    size_t occupied_count;
    const char** snake_ids;
    MoveDirection* moves;
    int* option_counts;
    MoveDirection (*options)[4];
    int snake_capacity;
    int frame_capacity;
} CoreSearchWorkspace;

bool CoreSearchWorkspaceInit(CoreSearchWorkspace* workspace, int max_snakes, size_t cell_count);
void CoreSearchWorkspaceFree(CoreSearchWorkspace* workspace);
bool CoreSearchWorkspaceEnsure(CoreSearchWorkspace* workspace, int max_snakes, size_t cell_count);
void CoreSearchWorkspaceFillOccupied(CoreSearchWorkspace* workspace, const Board* board, bool include_tails);

static inline size_t CoreSearchWorkspaceFrameOffset(const CoreSearchWorkspace* workspace, int ply) {
    if (ply < 0 || ply >= workspace->frame_capacity) {
        return 0;
    }
    return (size_t)ply * (size_t)workspace->snake_capacity;
}

static inline const char** CoreSearchWorkspaceSnakeIds(CoreSearchWorkspace* workspace, int ply) {
    return workspace->snake_ids + CoreSearchWorkspaceFrameOffset(workspace, ply);
}

static inline MoveDirection* CoreSearchWorkspaceMoves(CoreSearchWorkspace* workspace, int ply) {
    return workspace->moves + CoreSearchWorkspaceFrameOffset(workspace, ply);
}

static inline int* CoreSearchWorkspaceOptionCounts(CoreSearchWorkspace* workspace, int ply) {
    return workspace->option_counts + CoreSearchWorkspaceFrameOffset(workspace, ply);
}

static inline MoveDirection (*CoreSearchWorkspaceOptions(CoreSearchWorkspace* workspace, int ply))[4] {
    return workspace->options + CoreSearchWorkspaceFrameOffset(workspace, ply);
}
