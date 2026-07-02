#pragma once

#include "coord.h"

typedef struct {
    char* id;
    char* name;
    int health;
    Coord* body;
    int body_len;
    int length;
} Snake;

void SnakeInit(Snake* snake, const char* id, const char* name, int health, const Coord* body, int body_len);

void SnakeCopy(Snake* target, const Snake* source);

void SnakeFree(Snake* snake);

Coord SnakeHead(const Snake* snake);

