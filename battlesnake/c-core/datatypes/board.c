#include "board.h"

#include <stdlib.h>
#include <string.h>

static bool coord_in_array(const Coord* coords, int count, Coord coord) {
    for (int i = 0; i < count; i++) {
        if (CoordEquals(coords[i], coord)) {
            return true;
        }
    }
    return false;
}

static bool is_constrictor(const Board* board) {
    return board->ruleset_name != NULL && strcmp(board->ruleset_name, "constrictor") == 0;
}

static int snake_length(const Snake* snake) {
    return snake->length > 0 ? snake->length : snake->body_len;
}

static bool snake_can_eat_next_turn(const Board* board, const Snake* snake) {
    if (snake == NULL || snake->body_len == 0) {
        return false;
    }
    Coord head = SnakeHead(snake);
    for (int move = MOVE_UP; move <= MOVE_RIGHT; move++) {
        Coord next = MoveStep(head, (MoveDirection)move);
        if (coord_in_array(board->food, board->food_count, next)) {
            return true;
        }
    }
    return false;
}

MoveDirection MoveDirectionFromString(const char* value) {
    if (value == NULL) {
        return MOVE_INVALID;
    }
    if (strcmp(value, "up") == 0) {
        return MOVE_UP;
    }
    if (strcmp(value, "down") == 0) {
        return MOVE_DOWN;
    }
    if (strcmp(value, "left") == 0) {
        return MOVE_LEFT;
    }
    if (strcmp(value, "right") == 0) {
        return MOVE_RIGHT;
    }
    return MOVE_INVALID;
}

const char* MoveDirectionToString(MoveDirection move) {
    switch (move) {
        case MOVE_UP:
            return "up";
        case MOVE_DOWN:
            return "down";
        case MOVE_LEFT:
            return "left";
        case MOVE_RIGHT:
            return "right";
        default:
            return "";
    }
}

Coord MoveStep(Coord coord, MoveDirection move) {
    switch (move) {
        case MOVE_UP:
            return (Coord){coord.x, coord.y + 1};
        case MOVE_DOWN:
            return (Coord){coord.x, coord.y - 1};
        case MOVE_LEFT:
            return (Coord){coord.x - 1, coord.y};
        case MOVE_RIGHT:
            return (Coord){coord.x + 1, coord.y};
        default:
            return coord;
    }
}

Board* BoardCreate(int width, int height, const char* ruleset_name, int hazard_damage) {
    Board* board = (Board*)calloc(1, sizeof(Board));
    if (board == NULL) {
        return NULL;
    }
    board->width = width;
    board->height = height;
    board->ruleset_name = strdup(ruleset_name != NULL ? ruleset_name : "standard");
    board->hazard_damage = hazard_damage;
    if (board->ruleset_name == NULL) {
        free(board);
        return NULL;
    }
    return board;
}

void BoardFree(Board* board) {
    if (board == NULL) {
        return;
    }
    for (int i = 0; i < board->snake_count; i++) {
        SnakeFree(&board->snakes[i]);
    }
    free(board->snakes);
    free(board->food);
    free(board->hazards);
    free(board->ruleset_name);
    free(board);
}

Board* BoardCopy(const Board* board) {
    Board* copy = BoardCreate(board->width, board->height, board->ruleset_name, board->hazard_damage);
    if (copy == NULL) {
        return NULL;
    }
    for (int i = 0; i < board->snake_count; i++) {
        if (!BoardAddSnake(copy, &board->snakes[i])) {
            BoardFree(copy);
            return NULL;
        }
    }
    for (int i = 0; i < board->food_count; i++) {
        if (!BoardAddFood(copy, board->food[i])) {
            BoardFree(copy);
            return NULL;
        }
    }
    for (int i = 0; i < board->hazard_count; i++) {
        if (!BoardAddHazard(copy, board->hazards[i])) {
            BoardFree(copy);
            return NULL;
        }
    }
    return copy;
}

bool BoardAddSnake(Board* board, const Snake* snake) {
    Snake* resized = (Snake*)realloc(board->snakes, (size_t)(board->snake_count + 1) * sizeof(Snake));
    if (resized == NULL) {
        return false;
    }
    board->snakes = resized;
    SnakeCopy(&board->snakes[board->snake_count], snake);
    board->snake_count++;
    return true;
}

bool BoardAddFood(Board* board, Coord coord) {
    Coord* resized = (Coord*)realloc(board->food, (size_t)(board->food_count + 1) * sizeof(Coord));
    if (resized == NULL) {
        return false;
    }
    board->food = resized;
    board->food[board->food_count++] = coord;
    return true;
}

bool BoardAddHazard(Board* board, Coord coord) {
    Coord* resized = (Coord*)realloc(board->hazards, (size_t)(board->hazard_count + 1) * sizeof(Coord));
    if (resized == NULL) {
        return false;
    }
    board->hazards = resized;
    board->hazards[board->hazard_count++] = coord;
    return true;
}

Snake* BoardFindSnake(Board* board, const char* snake_id) {
    for (int i = 0; i < board->snake_count; i++) {
        if (strcmp(board->snakes[i].id, snake_id) == 0) {
            return &board->snakes[i];
        }
    }
    return NULL;
}

const Snake* BoardFindSnakeConst(const Board* board, const char* snake_id) {
    for (int i = 0; i < board->snake_count; i++) {
        if (strcmp(board->snakes[i].id, snake_id) == 0) {
            return &board->snakes[i];
        }
    }
    return NULL;
}

bool BoardInBounds(const Board* board, Coord coord) {
    return coord.x >= 0 && coord.x < board->width && coord.y >= 0 && coord.y < board->height;
}

int BoardOccupied(const Board* board, bool include_tails, Coord* out_coords, int max_coords) {
    int count = 0;
    for (int i = 0; i < board->snake_count; i++) {
        const Snake* snake = &board->snakes[i];
        int limit = include_tails ? snake->body_len : snake->body_len - 1;
        if (limit < 0) {
            limit = 0;
        }
        for (int j = 0; j < limit; j++) {
            if (out_coords != NULL && count < max_coords) {
                out_coords[count] = snake->body[j];
            }
            count++;
        }
    }
    return count;
}

bool BoardIsSafe(const Board* board, Coord coord, const char* snake_id) {
    const Snake* snake = BoardFindSnakeConst(board, snake_id);
    if (snake == NULL || !BoardInBounds(board, coord)) {
        return false;
    }

    int occupied_count = BoardOccupied(board, true, NULL, 0);
    Coord* occupied = NULL;
    Coord* tail_vacates = NULL;
    int tail_vacates_count = 0;

    if (occupied_count > 0) {
        occupied = (Coord*)malloc((size_t)occupied_count * sizeof(Coord));
        tail_vacates = (Coord*)malloc((size_t)board->snake_count * sizeof(Coord));
        if (occupied == NULL || tail_vacates == NULL) {
            free(occupied);
            free(tail_vacates);
            return false;
        }
        BoardOccupied(board, true, occupied, occupied_count);
    }

    for (int i = 0; i < board->snake_count; i++) {
        const Snake* other = &board->snakes[i];
        if (other->body_len == 0 || is_constrictor(board)) {
            continue;
        }
        if (strcmp(other->id, snake_id) == 0) {
            if (!coord_in_array(board->food, board->food_count, coord)) {
                tail_vacates[tail_vacates_count++] = other->body[other->body_len - 1];
            }
            continue;
        }
        if (!snake_can_eat_next_turn(board, other)) {
            tail_vacates[tail_vacates_count++] = other->body[other->body_len - 1];
        }
    }

    bool blocked = false;
    for (int i = 0; i < occupied_count; i++) {
        if (CoordEquals(occupied[i], coord) && !coord_in_array(tail_vacates, tail_vacates_count, coord)) {
            blocked = true;
            break;
        }
    }
    free(occupied);
    free(tail_vacates);
    if (blocked) {
        return false;
    }

    if (coord_in_array(board->hazards, board->hazard_count, coord) && snake->health <= board->hazard_damage + 1) {
        return false;
    }

    int own_length = snake_length(snake);
    for (int i = 0; i < board->snake_count; i++) {
        const Snake* other = &board->snakes[i];
        if (strcmp(other->id, snake_id) == 0 || other->body_len == 0) {
            continue;
        }
        Coord other_head = SnakeHead(other);
        for (int move = MOVE_UP; move <= MOVE_RIGHT; move++) {
            if (CoordEquals(coord, MoveStep(other_head, (MoveDirection)move)) && snake_length(other) >= own_length) {
                return false;
            }
        }
    }

    return true;
}

int BoardSafeMoves(const Board* board, const char* snake_id, MoveDirection out_moves[4]) {
    const Snake* snake = BoardFindSnakeConst(board, snake_id);
    if (snake == NULL || snake->body_len == 0) {
        return 0;
    }
    Coord head = SnakeHead(snake);
    int count = 0;
    for (int move = MOVE_UP; move <= MOVE_RIGHT; move++) {
        if (BoardIsSafe(board, MoveStep(head, (MoveDirection)move), snake_id)) {
            out_moves[count++] = (MoveDirection)move;
        }
    }
    return count;
}

static MoveDirection move_for_snake(const char* snake_id, const char** snake_ids, const MoveDirection* moves, int move_count) {
    for (int i = 0; i < move_count; i++) {
        if (strcmp(snake_id, snake_ids[i]) == 0) {
            return moves[i];
        }
    }
    return MOVE_INVALID;
}

Board* BoardCloneAndApply(const Board* board, const char** snake_ids, const MoveDirection* moves, int move_count) {
    Board* next = BoardCreate(board->width, board->height, board->ruleset_name, board->hazard_damage);
    if (next == NULL) {
        return NULL;
    }

    Coord* new_heads = (Coord*)calloc((size_t)board->snake_count, sizeof(Coord));
    bool* dead = (bool*)calloc((size_t)board->snake_count, sizeof(bool));
    bool* moved_flags = (bool*)calloc((size_t)board->snake_count, sizeof(bool));
    if (new_heads == NULL || dead == NULL || moved_flags == NULL) {
        free(new_heads);
        free(dead);
        free(moved_flags);
        BoardFree(next);
        return NULL;
    }

    for (int i = 0; i < board->snake_count; i++) {
        const Snake* snake = &board->snakes[i];
        MoveDirection move = move_for_snake(snake->id, snake_ids, moves, move_count);
        if (move == MOVE_INVALID || snake->body_len == 0) {
            dead[i] = true;
            BoardAddSnake(next, snake);
            continue;
        }

        Coord new_head = MoveStep(SnakeHead(snake), move);
        new_heads[i] = new_head;
        moved_flags[i] = true;
        int health = snake->health - 1;
        if (coord_in_array(board->hazards, board->hazard_count, new_head)) {
            health -= board->hazard_damage;
        }
        bool ate_food = coord_in_array(board->food, board->food_count, new_head);
        bool grew = ate_food || is_constrictor(board);
        if (ate_food) {
            health = 100;
        }

        int new_body_len = snake->body_len + (grew ? 1 : 0);
        Coord* new_body = (Coord*)malloc((size_t)new_body_len * sizeof(Coord));
        if (new_body == NULL) {
            dead[i] = true;
            continue;
        }
        new_body[0] = new_head;
        int copy_count = grew ? snake->body_len : snake->body_len - 1;
        if (copy_count > 0) {
            memcpy(&new_body[1], snake->body, (size_t)copy_count * sizeof(Coord));
        }

        Snake moved;
        SnakeInit(&moved, snake->id, snake->name, health, new_body, new_body_len);
        free(new_body);
        moved.length = new_body_len;
        BoardAddSnake(next, &moved);
        SnakeFree(&moved);

        if (!BoardInBounds(board, new_head) || health <= 0) {
            dead[i] = true;
        }
    }

    for (int i = 0; i < next->snake_count; i++) {
        if (dead[i]) {
            continue;
        }
        Coord head = new_heads[i];
        for (int j = 0; j < next->snake_count; j++) {
            const Snake* other = &next->snakes[j];
            for (int k = 1; k < other->body_len; k++) {
                if (CoordEquals(head, other->body[k])) {
                    dead[i] = true;
                }
            }
        }
    }

    for (int i = 0; i < next->snake_count; i++) {
        if (dead[i]) {
            continue;
        }
        for (int j = i + 1; j < next->snake_count; j++) {
            if (dead[j] || !CoordEquals(new_heads[i], new_heads[j])) {
                continue;
            }
            int left_len = snake_length(&next->snakes[i]);
            int right_len = snake_length(&next->snakes[j]);
            if (left_len > right_len) {
                dead[j] = true;
            } else if (right_len > left_len) {
                dead[i] = true;
            } else {
                dead[i] = true;
                dead[j] = true;
            }
        }
    }

    Board* resolved = BoardCreate(board->width, board->height, board->ruleset_name, board->hazard_damage);
    if (resolved == NULL) {
        free(new_heads);
        free(dead);
        free(moved_flags);
        BoardFree(next);
        return NULL;
    }

    for (int i = 0; i < next->snake_count; i++) {
        if (!dead[i]) {
            BoardAddSnake(resolved, &next->snakes[i]);
        }
    }
    for (int i = 0; i < board->food_count; i++) {
        bool eaten = false;
        for (int j = 0; j < board->snake_count; j++) {
            if (moved_flags[j] && CoordEquals(new_heads[j], board->food[i])) {
                eaten = true;
                break;
            }
        }
        if (!eaten) {
            BoardAddFood(resolved, board->food[i]);
        }
    }
    for (int i = 0; i < board->hazard_count; i++) {
        BoardAddHazard(resolved, board->hazards[i]);
    }

    free(new_heads);
    free(dead);
    free(moved_flags);
    BoardFree(next);
    return resolved;
}
