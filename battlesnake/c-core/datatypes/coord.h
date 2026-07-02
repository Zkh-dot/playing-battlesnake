#pragma once

#include <stdbool.h>

typedef struct {
    int x;
    int y;
} Coord;

bool CoordEquals(Coord left, Coord right);

