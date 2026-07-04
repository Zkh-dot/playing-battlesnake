#include "../../battlesnake/c-core/server/arena.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void test_alloc_and_reset(void) {
    BsArena arena;
    assert(BsArenaInit(&arena, 64));
    char* first = (char*)BsArenaAlloc(&arena, 8);
    assert(first != 0);
    memcpy(first, "abc", 4);
    assert(strcmp(first, "abc") == 0);
    BsArenaReset(&arena);
    char* second = (char*)BsArenaAlloc(&arena, 8);
    assert(second == first);
    BsArenaFree(&arena);
}

static void test_overflow_returns_null(void) {
    BsArena arena;
    assert(BsArenaInit(&arena, 16));
    assert(BsArenaAlloc(&arena, 32) == 0);
    assert(BsArenaHadOverflow(&arena));
    BsArenaFree(&arena);
}

static void test_size_max_boundaries(void) {
    BsArena arena;
    assert(BsArenaInit(&arena, 16));
    assert(BsArenaAlloc(&arena, SIZE_MAX) == 0);
    assert(BsArenaHadOverflow(&arena));
    BsArenaFree(&arena);

    assert(BsArenaInit(&arena, 16));
    assert(BsArenaStrDup(&arena, "x", SIZE_MAX) == 0);
    assert(BsArenaHadOverflow(&arena));
    BsArenaFree(&arena);
}

int main(void) {
    test_alloc_and_reset();
    test_overflow_returns_null();
    test_size_max_boundaries();
    return 0;
}
