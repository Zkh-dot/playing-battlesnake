#pragma once
#include <stdint.h>
#include "../datatypes/board.h"
uint64_t CoreZobristHashBoard(const Board* board);
uint64_t CoreZobristHashMove(uint64_t hash, int snake_index, Coord old_head, Coord new_head, MoveDirection move);
