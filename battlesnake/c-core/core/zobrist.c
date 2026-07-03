#include "zobrist.h"

#include <stdint.h>
#include <string.h>

static uint64_t mix64(uint64_t value) {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

static uint64_t string_hash(const char* value) {
    uint64_t hash = 14695981039346656037ULL;
    if (value == NULL) {
        value = "";
    }
    for (size_t i = 0; i < strlen(value); i++) {
        hash ^= (uint64_t)(unsigned char)value[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

static uint64_t coord_key(uint64_t domain, int index, Coord coord, uint64_t extra) {
    uint64_t key = ((uint64_t)(uint32_t)coord.x << 32) ^ (uint64_t)(uint32_t)coord.y;
    key ^= ((uint64_t)(uint32_t)index << 16);
    key ^= domain << 8;
    key ^= mix64(extra);
    return mix64(key);
}

uint64_t CoreZobristHashBoard(const Board* board) {
    if (board == NULL) {
        return 0;
    }

    uint64_t hash = 0;
    hash ^= mix64(((uint64_t)(uint32_t)board->width << 32) ^ (uint64_t)(uint32_t)board->height);
    hash ^= mix64(string_hash(board->ruleset_name) ^ 0x100000000ULL);
    hash ^= mix64((uint64_t)(uint32_t)board->hazard_damage ^ 0x200000000ULL);

    for (int i = 0; i < board->snake_count; i++) {
        const Snake* snake = &board->snakes[i];
        hash ^= mix64(string_hash(snake->id) ^ ((uint64_t)(uint32_t)i << 32) ^ 0x300000000ULL);
        hash ^= mix64((uint64_t)(uint32_t)snake->health ^ ((uint64_t)(uint32_t)i << 32) ^ 0x400000000ULL);
        hash ^= mix64((uint64_t)(uint32_t)snake->length ^ ((uint64_t)(uint32_t)i << 32) ^ 0x500000000ULL);

        for (int j = 0; j < snake->body_len; j++) {
            hash ^= coord_key(1, i, snake->body[j], (uint64_t)(uint32_t)j);
        }
    }

    for (int i = 0; i < board->food_count; i++) {
        hash ^= coord_key(2, i, board->food[i], 0);
    }

    for (int i = 0; i < board->hazard_count; i++) {
        hash ^= coord_key(3, i, board->hazards[i], (uint64_t)(uint32_t)board->hazard_damage);
    }

    return hash;
}

uint64_t CoreZobristHashMove(uint64_t hash, int snake_index, Coord old_head, Coord new_head, MoveDirection move) {
    hash ^= coord_key(4, snake_index, old_head, (uint64_t)(uint32_t)move);
    hash ^= coord_key(4, snake_index, new_head, (uint64_t)(uint32_t)move);
    return hash;
}
