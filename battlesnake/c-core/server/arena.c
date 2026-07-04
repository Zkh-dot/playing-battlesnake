#include "arena.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

bool BsArenaInit(BsArena* arena, size_t capacity) {
    if (arena == 0 || capacity == 0) {
        return false;
    }

    arena->data = (unsigned char*)malloc(capacity);
    arena->capacity = capacity;
    arena->offset = 0;
    arena->overflowed = arena->data == 0;
    return arena->data != 0;
}

void BsArenaReset(BsArena* arena) {
    if (arena == 0) {
        return;
    }

    arena->offset = 0;
    arena->overflowed = false;
}

void BsArenaFree(BsArena* arena) {
    if (arena == 0) {
        return;
    }

    free(arena->data);
    arena->data = 0;
    arena->capacity = 0;
    arena->offset = 0;
    arena->overflowed = false;
}

void* BsArenaAlloc(BsArena* arena, size_t size) {
    if (arena == 0 || arena->data == 0) {
        return 0;
    }

    size_t alignment = _Alignof(max_align_t);
    if (size > SIZE_MAX - (alignment - 1u)) {
        arena->overflowed = true;
        return 0;
    }

    size_t aligned = (size + (alignment - 1u)) & ~(alignment - 1u);
    if (aligned > arena->capacity || arena->offset > arena->capacity - aligned) {
        arena->overflowed = true;
        return 0;
    }

    void* result = arena->data + arena->offset;
    arena->offset += aligned;
    memset(result, 0, aligned);
    return result;
}

char* BsArenaStrDup(BsArena* arena, const char* start, size_t length) {
    if (length == SIZE_MAX) {
        if (arena != 0) {
            arena->overflowed = true;
        }
        return 0;
    }

    char* result = (char*)BsArenaAlloc(arena, length + 1);
    if (result == 0) {
        return 0;
    }

    memcpy(result, start, length);
    result[length] = '\0';
    return result;
}

bool BsArenaHadOverflow(const BsArena* arena) {
    return arena != 0 && arena->overflowed;
}
