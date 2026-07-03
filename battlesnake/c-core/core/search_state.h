#pragma once

#include <stdbool.h>

#include "../datatypes/board.h"

typedef struct {
    char* id;
    char* name;
    int health;
    Coord* body;
    int body_len;
    int length;
} CoreUndoSnake;

typedef struct {
    CoreUndoSnake* snakes;
    int snake_count;
    Coord* food;
    int food_count;
} CoreUndoBoardFrame;

typedef struct {
    Board board;
    int food_capacity;
    CoreUndoBoardFrame* undo_stack;
    int undo_count;
    int undo_capacity;
    Coord* new_heads;
    bool* dead;
    bool* moved_flags;
    int scratch_capacity;
} CoreSearchState;

bool CoreSearchStateInit(CoreSearchState* state, const Board* board);
void CoreSearchStateFree(CoreSearchState* state);
bool CoreSearchStateMakeMoves(CoreSearchState* state, const char** snake_ids, const MoveDirection* moves, int move_count);
bool CoreSearchStateUnmake(CoreSearchState* state);
const Board* CoreSearchStateBoard(const CoreSearchState* state);
