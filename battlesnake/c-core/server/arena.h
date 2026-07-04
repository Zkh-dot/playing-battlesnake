#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    unsigned char* data;
    size_t capacity;
    size_t offset;
    bool overflowed;
} BsArena;

bool BsArenaInit(BsArena* arena, size_t capacity);
void BsArenaReset(BsArena* arena);
void BsArenaFree(BsArena* arena);
void* BsArenaAlloc(BsArena* arena, size_t size);
char* BsArenaStrDup(BsArena* arena, const char* start, size_t length);
bool BsArenaHadOverflow(const BsArena* arena);
