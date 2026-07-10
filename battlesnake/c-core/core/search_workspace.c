#include "search_workspace.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int coord_index(const Board* board, Coord coord) {
    return coord.y * board->width + coord.x;
}

static bool search_workspace_resize_bytes(void** target, size_t byte_count) {
    if (byte_count == 0) {
        free(*target);
        *target = NULL;
        return true;
    }
    void* next = malloc(byte_count);
    if (next == NULL) {
        return false;
    }
    free(*target);
    *target = next;
    return true;
}

bool CoreSearchWorkspaceInit(CoreSearchWorkspace* workspace, int max_snakes, size_t cell_count) {
    if (workspace == NULL) {
        return false;
    }
    memset(workspace, 0, sizeof(*workspace));
    return CoreSearchWorkspaceEnsure(workspace, max_snakes, cell_count);
}

void CoreSearchWorkspaceFree(CoreSearchWorkspace* workspace) {
    if (workspace == NULL) {
        return;
    }
    free(workspace->occupied);
    free(workspace->snake_ids);
    free(workspace->moves);
    free(workspace->transition_causes);
    free(workspace->option_counts);
    free(workspace->options);
    memset(workspace, 0, sizeof(*workspace));
}

bool CoreSearchWorkspaceEnsure(CoreSearchWorkspace* workspace, int max_snakes, size_t cell_count) {
    if (workspace == NULL || max_snakes < 0) {
        return false;
    }
    if (cell_count > workspace->occupied_count) {
        if (!search_workspace_resize_bytes((void**)&workspace->occupied, cell_count * sizeof(unsigned char))) {
            return false;
        }
        workspace->occupied_count = cell_count;
    }

    int frame_capacity = CORE_SEARCH_MAX_DEPTH + 1;
    if (max_snakes <= workspace->snake_capacity && frame_capacity <= workspace->frame_capacity) {
        return true;
    }
    if (max_snakes > 0 && (size_t)max_snakes > SIZE_MAX / (size_t)frame_capacity) {
        return false;
    }
    size_t entry_count = (size_t)max_snakes * (size_t)frame_capacity;
    const char** snake_ids = NULL;
    MoveDirection* moves = NULL;
    uint32_t* transition_causes = NULL;
    int* option_counts = NULL;
    MoveDirection (*options)[4] = NULL;
    if (entry_count > 0) {
        snake_ids = (const char**)malloc(entry_count * sizeof(char*));
        moves = (MoveDirection*)malloc(entry_count * sizeof(MoveDirection));
        transition_causes = (uint32_t*)malloc(entry_count * sizeof(uint32_t));
        option_counts = (int*)malloc(entry_count * sizeof(int));
        options = (MoveDirection(*)[4])malloc(entry_count * sizeof(MoveDirection[4]));
        if (snake_ids == NULL || moves == NULL || transition_causes == NULL || option_counts == NULL || options == NULL) {
            free(snake_ids);
            free(moves);
            free(transition_causes);
            free(option_counts);
            free(options);
            return false;
        }
    }
    free(workspace->snake_ids);
    free(workspace->moves);
    free(workspace->transition_causes);
    free(workspace->option_counts);
    free(workspace->options);
    workspace->snake_ids = snake_ids;
    workspace->moves = moves;
    workspace->transition_causes = transition_causes;
    workspace->option_counts = option_counts;
    workspace->options = options;
    workspace->snake_capacity = max_snakes;
    workspace->frame_capacity = frame_capacity;
    return true;
}

void CoreSearchWorkspaceFillOccupied(CoreSearchWorkspace* workspace, const Board* board, bool include_tails) {
    if (workspace == NULL || board == NULL || workspace->occupied == NULL) {
        return;
    }
    memset(workspace->occupied, 0, workspace->occupied_count * sizeof(unsigned char));
    for (int i = 0; i < board->snake_count; i++) {
        const Snake* snake = &board->snakes[i];
        int limit = snake->body_len;
        if (!include_tails && limit > 0) {
            limit--;
        }
        for (int j = 0; j < limit; j++) {
            Coord coord = snake->body[j];
            if (BoardInBounds(board, coord)) {
                workspace->occupied[coord_index(board, coord)] = 1;
            }
        }
    }
}
