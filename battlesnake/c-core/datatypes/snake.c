#include "snake.h"

#include <stdlib.h>
#include <string.h>

static char* duplicate_string(const char* value) {
    if (value == NULL) {
        value = "";
    }
    char* copy = strdup(value);
    return copy;
}

void SnakeInit(Snake* snake, const char* id, const char* name, int health, const Coord* body, int body_len) {
    snake->id = duplicate_string(id);
    snake->name = duplicate_string(name);
    snake->health = health;
    snake->body_len = body_len > 0 ? body_len : 0;
    snake->length = snake->body_len;
    snake->body = NULL;

    if (snake->body_len > 0) {
        snake->body = (Coord*)malloc((size_t)snake->body_len * sizeof(Coord));
        if (snake->body != NULL) {
            memcpy(snake->body, body, (size_t)snake->body_len * sizeof(Coord));
        } else {
            snake->body_len = 0;
            snake->length = 0;
        }
    }
}

void SnakeCopy(Snake* target, const Snake* source) {
    SnakeInit(target, source->id, source->name, source->health, source->body, source->body_len);
    target->length = source->length;
}

void SnakeFree(Snake* snake) {
    if (snake == NULL) {
        return;
    }
    free(snake->id);
    free(snake->name);
    free(snake->body);
    snake->id = NULL;
    snake->name = NULL;
    snake->body = NULL;
    snake->body_len = 0;
    snake->length = 0;
    snake->health = 0;
}

Coord SnakeHead(const Snake* snake) {
    if (snake == NULL || snake->body_len == 0) {
        return (Coord){0, 0};
    }
    return snake->body[0];
}

