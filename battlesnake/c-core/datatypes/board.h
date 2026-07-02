#pragma once

#include <stdbool.h>

#include "coord.h"
#include "snake.h"

typedef enum {
    MOVE_INVALID = -1,
    MOVE_UP = 0,
    MOVE_DOWN = 1,
    MOVE_LEFT = 2,
    MOVE_RIGHT = 3,
} MoveDirection;

typedef struct {
    int width;
    int height;
    Snake* snakes;
    int snake_count;
    Coord* food;
    int food_count;
    Coord* hazards;
    int hazard_count;
    char* ruleset_name;
    int hazard_damage;
} Board;

MoveDirection MoveDirectionFromString(const char* value);

const char* MoveDirectionToString(MoveDirection move);

Coord MoveStep(Coord coord, MoveDirection move);

Board* BoardCreate(int width, int height, const char* ruleset_name, int hazard_damage);

void BoardFree(Board* board);

Board* BoardCopy(const Board* board);

bool BoardAddSnake(Board* board, const Snake* snake);

bool BoardAddFood(Board* board, Coord coord);

bool BoardAddHazard(Board* board, Coord coord);

Snake* BoardFindSnake(Board* board, const char* snake_id);

const Snake* BoardFindSnakeConst(const Board* board, const char* snake_id);

bool BoardInBounds(const Board* board, Coord coord);

bool BoardIsSafe(const Board* board, Coord coord, const char* snake_id);

int BoardSafeMoves(const Board* board, const char* snake_id, MoveDirection out_moves[4]);

Board* BoardCloneAndApply(const Board* board, const char** snake_ids, const MoveDirection* moves, int move_count);

int BoardOccupied(const Board* board, bool include_tails, Coord* out_coords, int max_coords);

