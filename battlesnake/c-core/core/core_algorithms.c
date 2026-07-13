#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "core_algorithms.h"
#include "search_state.h"
#include "search_workspace.h"
#include "transposition_table.h"
#include "zobrist.h"

#include <float.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CORE_MINIMAX_MAX_DEPTH CORE_SEARCH_MAX_DEPTH
#define CORE_TERMINAL_SURVIVAL_MAX_STEP 1000.0
#define CORE_SPACE_TIME_NEVER (INT_MAX / 2)

typedef struct CoreSearchTimer CoreSearchTimer;
static bool core_search_timed_out(const CoreSearchTimer* timer);

const char* CoreNotImplementedMessage(void) {
    return "core algorithm unavailable";
}

static char* core_duplicate_string(const char* value) {
    if (value == NULL) {
        value = "";
    }
    size_t length = strlen(value) + 1;
    char* copy = (char*)malloc(length);
    if (copy != NULL) {
        memcpy(copy, value, length);
    }
    return copy;
}

void CoreTerritoryFree(CoreTerritory* territory) {
    if (territory == NULL) {
        return;
    }
    for (int i = 0; i < territory->entry_count; i++) {
        free(territory->entries[i].snake_id);
        free(territory->entries[i].coords);
    }
    free(territory->entries);
    free(territory);
}

static int core_coord_index(const Board* board, Coord coord) {
    return coord.y * board->width + coord.x;
}

static int core_abs_int(int value) {
    return value < 0 ? -value : value;
}

static int core_manhattan(Coord left, Coord right) {
    return core_abs_int(left.x - right.x) + core_abs_int(left.y - right.y);
}

static bool core_coord_in_array(const Coord* coords, int count, Coord coord) {
    for (int i = 0; i < count; i++) {
        if (CoordEquals(coords[i], coord)) {
            return true;
        }
    }
    return false;
}

static bool core_is_constrictor(const Board* board) {
    return board->ruleset_name != NULL && strcmp(board->ruleset_name, "constrictor") == 0;
}

static int core_snake_length(const Snake* snake) {
    return snake->length > 0 ? snake->length : snake->body_len;
}

static bool core_snake_can_eat_next_turn(const Board* board, const Snake* snake) {
    if (snake == NULL || snake->body_len == 0) {
        return false;
    }

    Coord head = SnakeHead(snake);
    for (int move = MOVE_UP; move <= MOVE_RIGHT; move++) {
        Coord next = MoveStep(head, (MoveDirection)move);
        if (core_coord_in_array(board->food, board->food_count, next)) {
            return true;
        }
    }
    return false;
}

static int core_cell_count(const Board* board, size_t* out_count) {
    if (board == NULL || board->width <= 0 || board->height <= 0) {
        return 0;
    }
    size_t count = (size_t)board->width * (size_t)board->height;
    if (count > (size_t)INT_MAX) {
        return 0;
    }
    *out_count = count;
    return 1;
}

static int core_safe_moves_or_all(const Board* board, const char* snake_id, MoveDirection out_moves[4]) {
    int count = BoardSafeMoves(board, snake_id, out_moves);
    if (count > 0) {
        return count;
    }
    for (int move = MOVE_UP; move <= MOVE_RIGHT; move++) {
        out_moves[move] = (MoveDirection)move;
    }
    return 4;
}

static int core_command_moves(const Board* board, const char* snake_id, MoveDirection out_moves[4]) {
    const Snake* snake = BoardFindSnakeConst(board, snake_id);
    if (snake == NULL || snake->body_len == 0) {
        return 0;
    }

    Coord head = SnakeHead(snake);
    bool neck_is_vacating_tail = snake->body_len == 2 &&
        !core_is_constrictor(board) &&
        !core_coord_in_array(board->food, board->food_count, snake->body[1]);
    int count = 0;
    for (int move = MOVE_UP; move <= MOVE_RIGHT; move++) {
        Coord next = MoveStep(head, (MoveDirection)move);
        if (snake->body_len > 1 && !neck_is_vacating_tail && CoordEquals(next, snake->body[1])) {
            continue;
        }
        out_moves[count++] = (MoveDirection)move;
    }
    return count;
}

static void core_fill_movement_blocks(const Board* board, const char* snake_id, unsigned char* blocked) {
    for (int i = 0; i < board->snake_count; i++) {
        const Snake* snake = &board->snakes[i];
        int limit = snake->body_len - 1;
        if (limit < 0) {
            limit = 0;
        }
        for (int j = 0; j < limit; j++) {
            Coord body = snake->body[j];
            if (BoardInBounds(board, body)) {
                blocked[core_coord_index(board, body)] = 1;
            }
        }
    }

    const Snake* snake = BoardFindSnakeConst(board, snake_id);
    if (snake != NULL && snake->body_len > 0) {
        Coord head = SnakeHead(snake);
        if (BoardInBounds(board, head)) {
            blocked[core_coord_index(board, head)] = 0;
        }
        if (snake->health <= board->hazard_damage + 1) {
            for (int i = 0; i < board->hazard_count; i++) {
                Coord hazard = board->hazards[i];
                if (BoardInBounds(board, hazard)) {
                    blocked[core_coord_index(board, hazard)] = 1;
                }
            }
        }
    }
}

static bool core_is_edge_cell(const Board* board, Coord coord) {
    return coord.x == 0 || coord.y == 0 || coord.x == board->width - 1 || coord.y == board->height - 1;
}

static double core_center_score(const Board* board, Coord coord) {
    double center_x = ((double)board->width - 1.0) / 2.0;
    double center_y = ((double)board->height - 1.0) / 2.0;
    double max_distance = center_x + center_y;
    if (max_distance <= 0.0) {
        return 0.0;
    }
    double distance = (double)core_abs_int(coord.x - (int)center_x) + (double)core_abs_int(coord.y - (int)center_y);
    return max_distance - distance;
}

static int core_nearest_food_distance(const Board* board, Coord coord) {
    int best = INT_MAX;
    for (int i = 0; i < board->food_count; i++) {
        int distance = core_manhattan(coord, board->food[i]);
        if (distance < best) {
            best = distance;
        }
    }
    return best;
}

static bool core_collect_coords_from_marks(const Board* board, const unsigned char* marks, Coord** out_coords, int* out_count) {
    size_t cell_count = 0;
    if (!core_cell_count(board, &cell_count)) {
        return false;
    }

    int count = 0;
    for (size_t i = 0; i < cell_count; i++) {
        if (marks[i]) {
            count++;
        }
    }

    *out_coords = NULL;
    *out_count = count;
    if (count == 0) {
        return true;
    }

    Coord* coords = (Coord*)malloc((size_t)count * sizeof(Coord));
    if (coords == NULL) {
        return false;
    }

    int index = 0;
    for (size_t i = 0; i < cell_count; i++) {
        if (marks[i]) {
            coords[index++] = (Coord){(int)(i % (size_t)board->width), (int)(i / (size_t)board->width)};
        }
    }
    *out_coords = coords;
    return true;
}

CoreStatus CoreReachableSpace(const Board* board, Coord start, const char* snake_id, int* out_count) {
    if (board == NULL || snake_id == NULL || out_count == NULL || board->width <= 0 || board->height <= 0) {
        return CORE_ERROR;
    }

    *out_count = 0;
    const Snake* snake = BoardFindSnakeConst(board, snake_id);
    if (snake == NULL || !BoardInBounds(board, start)) {
        return CORE_OK;
    }

    size_t cell_count = (size_t)board->width * (size_t)board->height;
    unsigned char* blocked = (unsigned char*)calloc(cell_count, sizeof(unsigned char));
    unsigned char* seen = (unsigned char*)calloc(cell_count, sizeof(unsigned char));
    int* queue = (int*)malloc(cell_count * sizeof(int));
    if (blocked == NULL || seen == NULL || queue == NULL) {
        free(blocked);
        free(seen);
        free(queue);
        return CORE_ERROR;
    }

    for (int i = 0; i < board->snake_count; i++) {
        const Snake* other = &board->snakes[i];
        for (int j = 0; j < other->body_len; j++) {
            Coord body = other->body[j];
            if (BoardInBounds(board, body)) {
                blocked[core_coord_index(board, body)] = 1;
            }
        }
    }

    if (!core_is_constrictor(board)) {
        for (int i = 0; i < board->snake_count; i++) {
            const Snake* other = &board->snakes[i];
            if (other->body_len == 0) {
                continue;
            }

            Coord tail = other->body[other->body_len - 1];
            if (!BoardInBounds(board, tail)) {
                continue;
            }

            bool tail_vacates = false;
            if (strcmp(other->id, snake_id) == 0) {
                tail_vacates = !core_coord_in_array(board->food, board->food_count, tail);
            } else {
                tail_vacates = !core_snake_can_eat_next_turn(board, other);
            }

            if (tail_vacates) {
                blocked[core_coord_index(board, tail)] = 0;
            }
        }
    }

    if (snake->health <= board->hazard_damage + 1) {
        for (int i = 0; i < board->hazard_count; i++) {
            Coord hazard = board->hazards[i];
            if (BoardInBounds(board, hazard)) {
                blocked[core_coord_index(board, hazard)] = 1;
            }
        }
    }

    int own_length = core_snake_length(snake);
    for (int i = 0; i < board->snake_count; i++) {
        const Snake* other = &board->snakes[i];
        if (other->body_len == 0 || strcmp(other->id, snake_id) == 0 || core_snake_length(other) < own_length) {
            continue;
        }

        Coord other_head = SnakeHead(other);
        for (int move = MOVE_UP; move <= MOVE_RIGHT; move++) {
            Coord danger = MoveStep(other_head, (MoveDirection)move);
            if (BoardInBounds(board, danger)) {
                blocked[core_coord_index(board, danger)] = 1;
            }
        }
    }

    int start_index = core_coord_index(board, start);
    if (CoordEquals(start, SnakeHead(snake))) {
        blocked[start_index] = 0;
    }
    if (blocked[start_index]) {
        free(blocked);
        free(seen);
        free(queue);
        return CORE_OK;
    }

    int head = 0;
    int tail = 0;
    queue[tail++] = start_index;
    seen[start_index] = 1;

    while (head < tail) {
        int index = queue[head++];
        (*out_count)++;

        int x = index % board->width;
        int y = index / board->width;

        if (y + 1 < board->height) {
            int next = index + board->width;
            if (!seen[next] && !blocked[next]) {
                seen[next] = 1;
                queue[tail++] = next;
            }
        }
        if (y > 0) {
            int next = index - board->width;
            if (!seen[next] && !blocked[next]) {
                seen[next] = 1;
                queue[tail++] = next;
            }
        }
        if (x > 0) {
            int next = index - 1;
            if (!seen[next] && !blocked[next]) {
                seen[next] = 1;
                queue[tail++] = next;
            }
        }
        if (x + 1 < board->width) {
            int next = index + 1;
            if (!seen[next] && !blocked[next]) {
                seen[next] = 1;
                queue[tail++] = next;
            }
        }
    }

    free(blocked);
    free(seen);
    free(queue);
    return CORE_OK;
}

CoreStatus CoreShortestPath(
    const Board* board,
    Coord start,
    Coord goal,
    const char* snake_id,
    Coord** out_path,
    int* out_path_count
) {
    if (board == NULL || snake_id == NULL || out_path == NULL || out_path_count == NULL || board->width <= 0 || board->height <= 0) {
        return CORE_ERROR;
    }

    *out_path = NULL;
    *out_path_count = 0;

    const Snake* snake = BoardFindSnakeConst(board, snake_id);
    if (snake == NULL || !BoardInBounds(board, start) || !BoardInBounds(board, goal)) {
        return CORE_OK;
    }

    size_t cell_count = (size_t)board->width * (size_t)board->height;
    if (cell_count > (size_t)INT_MAX) {
        return CORE_ERROR;
    }

    unsigned char* blocked = (unsigned char*)calloc(cell_count, sizeof(unsigned char));
    unsigned char* closed = (unsigned char*)calloc(cell_count, sizeof(unsigned char));
    unsigned char* in_open = (unsigned char*)calloc(cell_count, sizeof(unsigned char));
    int* open = (int*)malloc(cell_count * sizeof(int));
    int* parent = (int*)malloc(cell_count * sizeof(int));
    int* g_score = (int*)malloc(cell_count * sizeof(int));
    int* f_score = (int*)malloc(cell_count * sizeof(int));
    if (blocked == NULL || closed == NULL || in_open == NULL || open == NULL || parent == NULL || g_score == NULL || f_score == NULL) {
        free(blocked);
        free(closed);
        free(in_open);
        free(open);
        free(parent);
        free(g_score);
        free(f_score);
        return CORE_ERROR;
    }

    for (size_t i = 0; i < cell_count; i++) {
        parent[i] = -1;
        g_score[i] = INT_MAX;
        f_score[i] = INT_MAX;
    }

    for (int i = 0; i < board->snake_count; i++) {
        const Snake* other = &board->snakes[i];
        for (int j = 0; j < other->body_len; j++) {
            Coord body = other->body[j];
            if (BoardInBounds(board, body)) {
                blocked[core_coord_index(board, body)] = 1;
            }
        }
    }

    if (!core_is_constrictor(board)) {
        for (int i = 0; i < board->snake_count; i++) {
            const Snake* other = &board->snakes[i];
            if (other->body_len == 0) {
                continue;
            }

            Coord tail = other->body[other->body_len - 1];
            if (!BoardInBounds(board, tail)) {
                continue;
            }

            bool tail_vacates = false;
            if (strcmp(other->id, snake_id) == 0) {
                tail_vacates = !core_coord_in_array(board->food, board->food_count, tail);
            } else {
                tail_vacates = !core_snake_can_eat_next_turn(board, other);
            }

            if (tail_vacates) {
                blocked[core_coord_index(board, tail)] = 0;
            }
        }
    }

    if (snake->health <= board->hazard_damage + 1) {
        for (int i = 0; i < board->hazard_count; i++) {
            Coord hazard = board->hazards[i];
            if (BoardInBounds(board, hazard)) {
                blocked[core_coord_index(board, hazard)] = 1;
            }
        }
    }

    int own_length = core_snake_length(snake);
    for (int i = 0; i < board->snake_count; i++) {
        const Snake* other = &board->snakes[i];
        if (other->body_len == 0 || strcmp(other->id, snake_id) == 0 || core_snake_length(other) < own_length) {
            continue;
        }

        Coord other_head = SnakeHead(other);
        for (int move = MOVE_UP; move <= MOVE_RIGHT; move++) {
            Coord danger = MoveStep(other_head, (MoveDirection)move);
            if (BoardInBounds(board, danger)) {
                blocked[core_coord_index(board, danger)] = 1;
            }
        }
    }

    int start_index = core_coord_index(board, start);
    int goal_index = core_coord_index(board, goal);
    blocked[start_index] = 0;
    if (blocked[goal_index]) {
        free(blocked);
        free(closed);
        free(in_open);
        free(open);
        free(parent);
        free(g_score);
        free(f_score);
        return CORE_OK;
    }

    int open_count = 1;
    open[0] = start_index;
    in_open[start_index] = 1;
    g_score[start_index] = 0;
    f_score[start_index] = core_manhattan(start, goal);

    while (open_count > 0) {
        int best_pos = 0;
        int best_index = open[0];
        for (int i = 1; i < open_count; i++) {
            int index = open[i];
            if (f_score[index] < f_score[best_index] || (f_score[index] == f_score[best_index] && g_score[index] > g_score[best_index])) {
                best_pos = i;
                best_index = index;
            }
        }

        int current = best_index;
        open[best_pos] = open[--open_count];
        in_open[current] = 0;

        if (current == goal_index) {
            int path_count = 1;
            for (int index = current; parent[index] >= 0; index = parent[index]) {
                path_count++;
            }

            Coord* path = (Coord*)malloc((size_t)path_count * sizeof(Coord));
            if (path == NULL) {
                free(blocked);
                free(closed);
                free(in_open);
                free(open);
                free(parent);
                free(g_score);
                free(f_score);
                return CORE_ERROR;
            }

            int index = current;
            for (int i = path_count - 1; i >= 0; i--) {
                path[i] = (Coord){index % board->width, index / board->width};
                index = parent[index];
            }

            *out_path = path;
            *out_path_count = path_count;
            break;
        }

        closed[current] = 1;
        int x = current % board->width;
        int y = current / board->width;
        int neighbors[4];
        int neighbor_count = 0;
        if (y + 1 < board->height) {
            neighbors[neighbor_count++] = current + board->width;
        }
        if (y > 0) {
            neighbors[neighbor_count++] = current - board->width;
        }
        if (x > 0) {
            neighbors[neighbor_count++] = current - 1;
        }
        if (x + 1 < board->width) {
            neighbors[neighbor_count++] = current + 1;
        }

        for (int i = 0; i < neighbor_count; i++) {
            int neighbor = neighbors[i];
            if (blocked[neighbor] || closed[neighbor]) {
                continue;
            }

            int tentative_g = g_score[current] + 1;
            if (tentative_g >= g_score[neighbor]) {
                continue;
            }

            Coord neighbor_coord = (Coord){neighbor % board->width, neighbor / board->width};
            parent[neighbor] = current;
            g_score[neighbor] = tentative_g;
            f_score[neighbor] = tentative_g + core_manhattan(neighbor_coord, goal);
            if (!in_open[neighbor]) {
                open[open_count++] = neighbor;
                in_open[neighbor] = 1;
            }
        }
    }

    free(blocked);
    free(closed);
    free(in_open);
    free(open);
    free(parent);
    free(g_score);
    free(f_score);
    return CORE_OK;
}

CoreStatus CoreVoronoiTerritory(const Board* board, void** out_territory) {
    if (board == NULL || out_territory == NULL || board->width <= 0 || board->height <= 0) {
        return CORE_ERROR;
    }

    *out_territory = NULL;

    size_t cell_count = (size_t)board->width * (size_t)board->height;
    if (cell_count > (size_t)INT_MAX) {
        return CORE_ERROR;
    }

    CoreTerritory* territory = (CoreTerritory*)calloc(1, sizeof(CoreTerritory));
    unsigned char* blocked = (unsigned char*)calloc(cell_count, sizeof(unsigned char));
    int* owner = (int*)malloc(cell_count * sizeof(int));
    int* distance = (int*)malloc(cell_count * sizeof(int));
    int* queue = (int*)malloc(cell_count * sizeof(int));
    if (territory == NULL || blocked == NULL || owner == NULL || distance == NULL || queue == NULL) {
        CoreTerritoryFree(territory);
        free(blocked);
        free(owner);
        free(distance);
        free(queue);
        return CORE_ERROR;
    }

    territory->entry_count = board->snake_count;
    if (board->snake_count > 0) {
        territory->entries = (CoreTerritoryEntry*)calloc((size_t)board->snake_count, sizeof(CoreTerritoryEntry));
        if (territory->entries == NULL) {
            CoreTerritoryFree(territory);
            free(blocked);
            free(owner);
            free(distance);
            free(queue);
            return CORE_ERROR;
        }
    }

    for (size_t i = 0; i < cell_count; i++) {
        owner[i] = -2;
        distance[i] = -1;
    }

    for (int i = 0; i < board->snake_count; i++) {
        const Snake* snake = &board->snakes[i];
        territory->entries[i].snake_id = core_duplicate_string(snake->id);
        if (territory->entries[i].snake_id == NULL) {
            CoreTerritoryFree(territory);
            free(blocked);
            free(owner);
            free(distance);
            free(queue);
            return CORE_ERROR;
        }

        for (int j = 0; j < snake->body_len; j++) {
            Coord body = snake->body[j];
            if (BoardInBounds(board, body)) {
                blocked[core_coord_index(board, body)] = 1;
            }
        }
    }

    int head = 0;
    int tail = 0;
    for (int i = 0; i < board->snake_count; i++) {
        const Snake* snake = &board->snakes[i];
        if (snake->body_len == 0) {
            continue;
        }

        Coord snake_head = SnakeHead(snake);
        if (!BoardInBounds(board, snake_head)) {
            continue;
        }

        int index = core_coord_index(board, snake_head);
        blocked[index] = 0;
        if (distance[index] < 0) {
            owner[index] = i;
            distance[index] = 0;
            queue[tail++] = index;
        } else if (distance[index] == 0 && owner[index] != i) {
            owner[index] = -1;
        }
    }

    while (head < tail) {
        int layer_end = tail;
        while (head < layer_end) {
            int current = queue[head++];
            int current_owner = owner[current];
            if (current_owner < 0) {
                continue;
            }

            int next_distance = distance[current] + 1;
            int x = current % board->width;
            int y = current / board->width;
            int neighbors[4];
            int neighbor_count = 0;
            if (y + 1 < board->height) {
                neighbors[neighbor_count++] = current + board->width;
            }
            if (y > 0) {
                neighbors[neighbor_count++] = current - board->width;
            }
            if (x > 0) {
                neighbors[neighbor_count++] = current - 1;
            }
            if (x + 1 < board->width) {
                neighbors[neighbor_count++] = current + 1;
            }

            for (int i = 0; i < neighbor_count; i++) {
                int neighbor = neighbors[i];
                if (blocked[neighbor]) {
                    continue;
                }

                if (distance[neighbor] < 0) {
                    owner[neighbor] = current_owner;
                    distance[neighbor] = next_distance;
                    queue[tail++] = neighbor;
                } else if (distance[neighbor] == next_distance && owner[neighbor] >= 0 && owner[neighbor] != current_owner) {
                    owner[neighbor] = -1;
                }
            }
        }
    }

    for (size_t i = 0; i < cell_count; i++) {
        if (owner[i] >= 0) {
            territory->entries[owner[i]].coord_count++;
        }
    }

    for (int i = 0; i < territory->entry_count; i++) {
        int coord_count = territory->entries[i].coord_count;
        if (coord_count <= 0) {
            continue;
        }

        territory->entries[i].coords = (Coord*)malloc((size_t)coord_count * sizeof(Coord));
        if (territory->entries[i].coords == NULL) {
            CoreTerritoryFree(territory);
            free(blocked);
            free(owner);
            free(distance);
            free(queue);
            return CORE_ERROR;
        }
        territory->entries[i].coord_count = 0;
    }

    for (size_t i = 0; i < cell_count; i++) {
        int cell_owner = owner[i];
        if (cell_owner >= 0) {
            CoreTerritoryEntry* entry = &territory->entries[cell_owner];
            entry->coords[entry->coord_count++] = (Coord){(int)(i % (size_t)board->width), (int)(i / (size_t)board->width)};
        }
    }

    free(blocked);
    free(owner);
    free(distance);
    free(queue);
    *out_territory = territory;
    return CORE_OK;
}

static void core_fill_vacate_times(const Board* board, const char* snake_id, int* vacate) {
    bool constrictor = core_is_constrictor(board);
    for (int i = 0; i < board->snake_count; i++) {
        const Snake* snake = &board->snakes[i];
        if (snake->body_len <= 0) {
            continue;
        }

        int delay = 0;
        if (constrictor) {
            delay = CORE_SPACE_TIME_NEVER;
        } else if (strcmp(snake->id, snake_id) == 0) {
            Coord tail = snake->body[snake->body_len - 1];
            if (core_coord_in_array(board->food, board->food_count, tail)) {
                delay = 1;
            }
        } else if (core_snake_can_eat_next_turn(board, snake)) {
            delay = 1;
        }

        for (int j = 0; j < snake->body_len; j++) {
            Coord body = snake->body[j];
            if (!BoardInBounds(board, body)) {
                continue;
            }

            int vacate_time = CORE_SPACE_TIME_NEVER;
            if (delay < CORE_SPACE_TIME_NEVER) {
                vacate_time = snake->body_len - j + delay;
                if (vacate_time > CORE_SPACE_TIME_NEVER) {
                    vacate_time = CORE_SPACE_TIME_NEVER;
                }
            }

            int index = core_coord_index(board, body);
            if (vacate_time > vacate[index]) {
                vacate[index] = vacate_time;
            }
        }
    }
}

static bool core_fill_opponent_arrival(
    const Board* board,
    const char* snake_id,
    const char* selected_opponent_id,
    int minimum_length,
    int maximum_length_exclusive,
    const int* vacate,
    int* opponent_arrival,
    unsigned char* exact_reachability,
    unsigned int* seen,
    unsigned int seen_stamp,
    size_t* queue,
    size_t cell_count,
    int max_time,
    const CoreSearchTimer* timer
) {
    for (size_t i = 0; i < cell_count; i++) {
        if ((i & 63u) == 0 && timer != NULL && core_search_timed_out(timer)) {
            return false;
        }
        opponent_arrival[i] = CORE_SPACE_TIME_NEVER;
    }

    size_t state_layers = (size_t)max_time + 1;
    if (cell_count != 0 && state_layers > ((size_t)-1) / cell_count) {
        return false;
    }
    if (seen == NULL || seen_stamp == 0 || queue == NULL) {
        return false;
    }

    size_t head = 0;
    size_t tail = 0;
    for (int i = 0; i < board->snake_count; i++) {
        if (timer != NULL && core_search_timed_out(timer)) {
            return false;
        }
        const Snake* snake = &board->snakes[i];
        int snake_length = core_snake_length(snake);
        if (snake->body_len == 0 || strcmp(snake->id, snake_id) == 0 ||
            (selected_opponent_id != NULL && strcmp(snake->id, selected_opponent_id) != 0) ||
            snake_length < minimum_length || snake_length >= maximum_length_exclusive) {
            continue;
        }

        Coord snake_head = SnakeHead(snake);
        if (!BoardInBounds(board, snake_head)) {
            continue;
        }

        int index = core_coord_index(board, snake_head);
        if (opponent_arrival[index] > 0) {
            opponent_arrival[index] = 0;
            seen[index] = seen_stamp;
            if (exact_reachability != NULL) {
                exact_reachability[index] = 1;
            }
            queue[tail++] = (size_t)index;
        }
    }

    while (head < tail) {
        if ((head & 63u) == 0 && timer != NULL && core_search_timed_out(timer)) {
            return false;
        }
        size_t state = queue[head++];
        int current = (int)(state % cell_count);
        int current_time = (int)(state / cell_count);
        if (current_time >= max_time) {
            continue;
        }

        int next_time = current_time + 1;
        int x = current % board->width;
        int y = current / board->width;
        int neighbors[4];
        int neighbor_count = 0;
        if (y + 1 < board->height) {
            neighbors[neighbor_count++] = current + board->width;
        }
        if (y > 0) {
            neighbors[neighbor_count++] = current - board->width;
        }
        if (x > 0) {
            neighbors[neighbor_count++] = current - 1;
        }
        if (x + 1 < board->width) {
            neighbors[neighbor_count++] = current + 1;
        }

        for (int i = 0; i < neighbor_count; i++) {
            int neighbor = neighbors[i];
            if (next_time < vacate[neighbor]) {
                continue;
            }

            size_t next_state = (size_t)next_time * cell_count + (size_t)neighbor;
            if (seen[next_state] == seen_stamp) {
                continue;
            }

            seen[next_state] = seen_stamp;
            if (exact_reachability != NULL) {
                exact_reachability[next_state] = 1;
            }
            queue[tail++] = next_state;
            if (opponent_arrival[neighbor] > next_time) {
                opponent_arrival[neighbor] = next_time;
            }
        }
    }

    return true;
}

static bool core_has_equal_or_longer_opponent(const Board* board, const char* snake_id, int own_length) {
    for (int i = 0; i < board->snake_count; i++) {
        const Snake* snake = &board->snakes[i];
        if (snake->body_len == 0 || strcmp(snake->id, snake_id) == 0) {
            continue;
        }
        if (core_snake_length(snake) >= own_length) {
            return true;
        }
    }
    return false;
}

static bool core_has_opponent(const Board* board, const char* snake_id) {
    for (int i = 0; i < board->snake_count; i++) {
        const Snake* snake = &board->snakes[i];
        if (snake->body_len > 0 && strcmp(snake->id, snake_id) != 0) {
            return true;
        }
    }
    return false;
}

typedef struct {
    int* vacate;
    int* opponent_arrival;
    int* own_arrival;
    unsigned int* opponent_seen;
    unsigned int* own_seen;
    size_t* opponent_queue;
    size_t* own_queue;
    size_t cell_capacity;
    size_t state_capacity;
    unsigned int opponent_stamp;
    unsigned int own_stamp;
} CoreSpaceTimeScratch;

static _Thread_local CoreSpaceTimeScratch core_space_time_scratch;

static bool core_space_time_resize(void** target, size_t byte_count) {
    void* next = realloc(*target, byte_count);
    if (next == NULL) {
        return false;
    }
    *target = next;
    return true;
}

static bool core_space_time_ensure_scratch(size_t cell_count, size_t state_count, CoreSpaceTimeScratch** out_scratch) {
    CoreSpaceTimeScratch* scratch = &core_space_time_scratch;
    if (cell_count > scratch->cell_capacity) {
        if (cell_count > ((size_t)-1) / sizeof(int)) {
            return false;
        }
        size_t int_bytes = cell_count * sizeof(int);
        if (!core_space_time_resize((void**)&scratch->vacate, int_bytes) ||
            !core_space_time_resize((void**)&scratch->opponent_arrival, int_bytes) ||
            !core_space_time_resize((void**)&scratch->own_arrival, int_bytes)) {
            return false;
        }
        scratch->cell_capacity = cell_count;
    }
    if (state_count > scratch->state_capacity) {
        if (state_count > ((size_t)-1) / sizeof(size_t)) {
            return false;
        }
        if (!core_space_time_resize((void**)&scratch->opponent_seen, state_count * sizeof(unsigned int)) ||
            !core_space_time_resize((void**)&scratch->own_seen, state_count * sizeof(unsigned int)) ||
            !core_space_time_resize((void**)&scratch->opponent_queue, state_count * sizeof(size_t)) ||
            !core_space_time_resize((void**)&scratch->own_queue, state_count * sizeof(size_t))) {
            return false;
        }
        memset(scratch->opponent_seen, 0, state_count * sizeof(unsigned int));
        memset(scratch->own_seen, 0, state_count * sizeof(unsigned int));
        scratch->state_capacity = state_count;
    }
    *out_scratch = scratch;
    return true;
}

static unsigned int core_space_time_next_stamp(
    unsigned int* stamp,
    unsigned int* seen,
    size_t state_count
) {
    if (*stamp == UINT_MAX) {
        memset(seen, 0, state_count * sizeof(unsigned int));
        *stamp = 0;
    }
    (*stamp)++;
    return *stamp;
}

CoreStatus CoreSpaceTimeCompute(
    const Board* board,
    const char* snake_id,
    CoreSpaceTimeMetrics* out_metrics
) {
    if (board == NULL || snake_id == NULL || out_metrics == NULL || board->width <= 0 || board->height <= 0) {
        return CORE_ERROR;
    }

    memset(out_metrics, 0, sizeof(*out_metrics));

    const Snake* snake = BoardFindSnakeConst(board, snake_id);
    if (snake == NULL || snake->body_len == 0) {
        return CORE_OK;
    }

    Coord head_coord = SnakeHead(snake);
    if (!BoardInBounds(board, head_coord)) {
        return CORE_OK;
    }

    size_t cell_count = 0;
    if (!core_cell_count(board, &cell_count)) {
        return CORE_ERROR;
    }

    size_t max_cell_count = cell_count;
    if (max_cell_count > (size_t)INT_MAX) {
        return CORE_ERROR;
    }

    CoreSpaceTimeScratch* scratch = NULL;
    if (!core_space_time_ensure_scratch(max_cell_count, 0, &scratch)) {
        return CORE_ERROR;
    }

    int* vacate = scratch->vacate;
    memset(vacate, 0, cell_count * sizeof(int));
    core_fill_vacate_times(board, snake_id, vacate);

    if (snake->health <= board->hazard_damage + 1) {
        for (int i = 0; i < board->hazard_count; i++) {
            Coord hazard = board->hazards[i];
            if (BoardInBounds(board, hazard)) {
                vacate[core_coord_index(board, hazard)] = CORE_SPACE_TIME_NEVER;
            }
        }
    }

    int max_vacate = 0;
    for (size_t i = 0; i < cell_count; i++) {
        if (vacate[i] < CORE_SPACE_TIME_NEVER && vacate[i] > max_vacate) {
            max_vacate = vacate[i];
        }
    }
    if (cell_count > (size_t)(INT_MAX - max_vacate - 2)) {
        return CORE_ERROR;
    }
    int max_time = max_vacate + (int)cell_count + 2;

    size_t state_layers = (size_t)max_time + 1;
    if (cell_count != 0 && state_layers > ((size_t)-1) / cell_count) {
        return CORE_ERROR;
    }
    size_t state_count = state_layers * cell_count;

    if (!core_space_time_ensure_scratch(max_cell_count, state_count, &scratch)) {
        return CORE_ERROR;
    }
    int* opponent_arrival = scratch->opponent_arrival;
    int* own_arrival = scratch->own_arrival;

    int own_length = core_snake_length(snake);
    if (core_has_equal_or_longer_opponent(board, snake_id, own_length)) {
        unsigned int opponent_stamp = core_space_time_next_stamp(
            &scratch->opponent_stamp,
            scratch->opponent_seen,
            state_count
        );
        if (!core_fill_opponent_arrival(
            board,
            snake_id,
            NULL,
            own_length,
            INT_MAX,
            vacate,
            opponent_arrival,
            NULL,
            scratch->opponent_seen,
            opponent_stamp,
            scratch->opponent_queue,
            cell_count,
            max_time,
            NULL
        )) {
            return CORE_ERROR;
        }
    } else {
        for (size_t i = 0; i < cell_count; i++) {
            opponent_arrival[i] = CORE_SPACE_TIME_NEVER;
        }
    }

    unsigned int* own_seen = scratch->own_seen;
    unsigned int own_stamp = core_space_time_next_stamp(&scratch->own_stamp, own_seen, state_count);
    size_t* queue = scratch->own_queue;

    for (size_t i = 0; i < cell_count; i++) {
        own_arrival[i] = CORE_SPACE_TIME_NEVER;
    }

    size_t head = 0;
    size_t tail = 0;
    int head_index = core_coord_index(board, head_coord);
    own_arrival[head_index] = 0;
    own_seen[head_index] = own_stamp;
    queue[tail++] = (size_t)head_index;

    int own_tail_index = -1;
    if (snake->body_len > 0) {
        Coord own_tail = snake->body[snake->body_len - 1];
        if (BoardInBounds(board, own_tail)) {
            own_tail_index = core_coord_index(board, own_tail);
        }
    }

    while (head < tail) {
        size_t state = queue[head++];
        int current = (int)(state % cell_count);
        int current_time = (int)(state / cell_count);
        if (current_time >= max_time) {
            continue;
        }

        int next_time = current_time + 1;
        int x = current % board->width;
        int y = current / board->width;
        int neighbors[4];
        int neighbor_count = 0;
        if (y + 1 < board->height) {
            neighbors[neighbor_count++] = current + board->width;
        }
        if (y > 0) {
            neighbors[neighbor_count++] = current - board->width;
        }
        if (x > 0) {
            neighbors[neighbor_count++] = current - 1;
        }
        if (x + 1 < board->width) {
            neighbors[neighbor_count++] = current + 1;
        }

        for (int i = 0; i < neighbor_count; i++) {
            int neighbor = neighbors[i];
            if (next_time < vacate[neighbor] ||
                opponent_arrival[neighbor] <= next_time) {
                continue;
            }

            size_t next_state = (size_t)next_time * cell_count + (size_t)neighbor;
            if (own_seen[next_state] == own_stamp) {
                continue;
            }

            own_seen[next_state] = own_stamp;
            queue[tail++] = next_state;
            if (own_arrival[neighbor] > next_time) {
                own_arrival[neighbor] = next_time;
            }
            if (neighbor != head_index && own_arrival[neighbor] == next_time) {
                out_metrics->reachable_cells++;
                if (next_time > out_metrics->max_arrival) {
                    out_metrics->max_arrival = next_time;
                }
            }
            if (neighbor == own_tail_index && next_time >= vacate[neighbor]) {
                out_metrics->tail_reachable = true;
            }
            if ((size_t)out_metrics->reachable_cells + 1 >= cell_count) {
                head = tail;
                break;
            }
        }
    }

    out_metrics->dead = !out_metrics->tail_reachable && out_metrics->reachable_cells < own_length;

    return CORE_OK;
}

struct CoreSearchTimer {
    struct timespec start;
    struct timespec deadline;
};

typedef struct {
    CoreSearchTimer timer;
    CoreSearchConfig config;
    CoreSearchStats* stats;
    CoreTranspositionTable tt;
    CoreSearchWorkspace workspace;
    CoreSearchState* state;
    const char* opponent_id;
    uint8_t root_allowed_mask;
    bool tt_enabled;
    bool root_best_valid;
    MoveDirection root_best_move;
    bool root_move_value_valid[4];
    CoreSearchValue root_move_values[4];
    MoveDirection principal_variation[CORE_MINIMAX_MAX_DEPTH + 1];
    MoveDirection killer_moves[CORE_MINIMAX_MAX_DEPTH + 1][2];
    int history_scores[4];
} CoreSearchContext;

static CoreSearchTimer core_search_timer_start(int time_budget_ms) {
    if (time_budget_ms < 1) {
        time_budget_ms = 1;
    }

    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    struct timespec deadline = start;
    deadline.tv_sec += time_budget_ms / 1000;
    deadline.tv_nsec += (long)(time_budget_ms % 1000) * 1000000L;
    while (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000L;
    }

    return (CoreSearchTimer){.start = start, .deadline = deadline};
}

static CoreSearchTimer core_search_timer_prefix(
    const CoreSearchTimer* overall,
    int prefix_budget_ms
) {
    CoreSearchTimer prefix = *overall;
    prefix.deadline = prefix.start;
    prefix.deadline.tv_sec += prefix_budget_ms / 1000;
    prefix.deadline.tv_nsec += (long)(prefix_budget_ms % 1000) * 1000000L;
    while (prefix.deadline.tv_nsec >= 1000000000L) {
        prefix.deadline.tv_sec++;
        prefix.deadline.tv_nsec -= 1000000000L;
    }
    if (prefix.deadline.tv_sec > overall->deadline.tv_sec ||
        (prefix.deadline.tv_sec == overall->deadline.tv_sec &&
         prefix.deadline.tv_nsec > overall->deadline.tv_nsec)) {
        prefix.deadline = overall->deadline;
    }
    return prefix;
}

static bool core_search_timed_out(const CoreSearchTimer* timer) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec > timer->deadline.tv_sec ||
        (now.tv_sec == timer->deadline.tv_sec && now.tv_nsec >= timer->deadline.tv_nsec);
}

static double core_elapsed_ms(struct timespec start, struct timespec end) {
    long seconds = end.tv_sec - start.tv_sec;
    long nanoseconds = end.tv_nsec - start.tv_nsec;
    return (double)seconds * 1000.0 + (double)nanoseconds / 1000000.0;
}

static bool core_valid_move_direction(MoveDirection move) {
    return move >= MOVE_UP && move <= MOVE_RIGHT;
}

static int core_find_move_index(const MoveDirection* moves, int move_count, MoveDirection move) {
    for (int i = 0; i < move_count; i++) {
        if (moves[i] == move) {
            return i;
        }
    }
    return INT_MAX;
}

static void core_move_to_front(MoveDirection* moves, int move_count, MoveDirection move) {
    int index = core_find_move_index(moves, move_count, move);
    if (index <= 0 || index == INT_MAX) {
        return;
    }
    MoveDirection selected = moves[index];
    for (int i = index; i > 0; i--) {
        moves[i] = moves[i - 1];
    }
    moves[0] = selected;
}

static int core_preferred_order_rank(
    const MoveDirection* moves,
    int move_count,
    MoveDirection move,
    MoveDirection preferred
) {
    int move_index = core_find_move_index(moves, move_count, move);
    int preferred_index = core_find_move_index(moves, move_count, preferred);
    if (preferred_index == INT_MAX) {
        return move_index;
    }
    if (move_index == preferred_index) {
        return 0;
    }
    return move_index < preferred_index ? move_index + 1 : move_index;
}

static int core_move_order_score(
    CoreSearchContext* context,
    int ply,
    MoveDirection move,
    MoveDirection tt_best,
    MoveDirection previous_iteration_best
) {
    int score = 0;
    if (move == tt_best) {
        score += 100000;
    }
    if (move == previous_iteration_best) {
        score += 50000;
    }
    if (ply >= 0 && ply <= CORE_MINIMAX_MAX_DEPTH && move == context->principal_variation[ply]) {
        score += 25000;
    }
    if (ply >= 0 && ply <= CORE_MINIMAX_MAX_DEPTH && move == context->killer_moves[ply][0]) {
        score += 12000;
    }
    if (ply >= 0 && ply <= CORE_MINIMAX_MAX_DEPTH && move == context->killer_moves[ply][1]) {
        score += 8000;
    }
    if (core_valid_move_direction(move)) {
        score += context->history_scores[(int)move];
    }
    return score;
}

static void core_order_moves(
    CoreSearchContext* context,
    int ply,
    MoveDirection* moves,
    int move_count,
    MoveDirection tt_best,
    MoveDirection previous_iteration_best
) {
    if (context == NULL) {
        return;
    }
    if (!context->config.enable_move_ordering) {
        return;
    }
    MoveDirection effective_preferred = core_valid_move_direction(tt_best) ? tt_best : previous_iteration_best;
    if (ply == 0) {
        core_move_to_front(moves, move_count, effective_preferred);
        return;
    }
    for (int i = 1; i < move_count; i++) {
        MoveDirection move = moves[i];
        int score = core_move_order_score(context, ply, move, tt_best, previous_iteration_best);
        int j = i - 1;
        while (j >= 0 && core_move_order_score(context, ply, moves[j], tt_best, previous_iteration_best) < score) {
            moves[j + 1] = moves[j];
            j--;
        }
        moves[j + 1] = move;
    }
}

CoreOutcome CoreClassifyDuelOutcome(
    const Board* board,
    const char* snake_id,
    const char* opponent_id
) {
    if (board == NULL || snake_id == NULL || opponent_id == NULL) {
        return CORE_OUTCOME_UNRESOLVED;
    }
    const Snake* snake = BoardFindSnakeConst(board, snake_id);
    const Snake* opponent = BoardFindSnakeConst(board, opponent_id);
    bool snake_alive = snake != NULL && snake->body_len > 0;
    bool opponent_alive = opponent != NULL && opponent->body_len > 0;
    if (snake_alive && opponent_alive) {
        return CORE_OUTCOME_UNRESOLVED;
    }
    if (snake_alive) {
        return CORE_OUTCOME_WIN;
    }
    if (opponent_alive) {
        return CORE_OUTCOME_LOSS;
    }
    return CORE_OUTCOME_DRAW;
}

static double core_terminal_survival_step(const CoreEvaluationWeights* weights) {
    double gap = weights->terminal_win - weights->terminal_loss;
    if (!(gap > 0.0)) {
        return 0.0;
    }

    double bounded_step = gap / (4.0 * (double)(CORE_MINIMAX_MAX_DEPTH + 1));
    return bounded_step < CORE_TERMINAL_SURVIVAL_MAX_STEP ?
        bounded_step :
        CORE_TERMINAL_SURVIVAL_MAX_STEP;
}

static CoreSearchValue core_unresolved_value(double score) {
    CoreSearchValue value;
    memset(&value, 0, sizeof(value));
    value.score = score;
    value.outcome = CORE_OUTCOME_UNRESOLVED;
    value.bound = CORE_VALUE_BOUND_EXACT;
    return value;
}

static CoreSearchValue core_terminal_value(
    const CoreEvaluationWeights* weights,
    CoreOutcome outcome,
    uint32_t cause,
    uint16_t distance
) {
    CoreSearchValue value;
    memset(&value, 0, sizeof(value));
    value.outcome = outcome;
    value.terminal_distance = distance;
    value.cause = cause;
    value.bound = CORE_VALUE_BOUND_EXACT;
    double step = core_terminal_survival_step(weights);
    if (outcome == CORE_OUTCOME_WIN) {
        value.score = weights->terminal_win - step * (double)distance;
    } else if (outcome == CORE_OUTCOME_LOSS) {
        value.score = weights->terminal_loss + step * (double)distance;
    } else {
        value.score = (weights->terminal_win + weights->terminal_loss) / 2.0;
    }
    return value;
}

static CoreSearchValue core_backup_child_value(
    const CoreEvaluationWeights* weights,
    CoreSearchValue child
) {
    if (child.outcome == CORE_OUTCOME_UNRESOLVED) {
        return child;
    }
    uint16_t distance = child.terminal_distance < UINT16_MAX ?
        (uint16_t)(child.terminal_distance + 1) : UINT16_MAX;
    CoreSearchValue value = core_terminal_value(weights, child.outcome, child.cause, distance);
    value.bound = child.bound;
    return value;
}

static int core_popcount4(uint8_t mask) {
    int count = 0;
    for (int bit = 0; bit < 4; bit++) {
        if ((mask & (1u << bit)) != 0) {
            count++;
        }
    }
    return count;
}

CoreStatus CoreDuelRootProfile(
    const Board* board,
    const char* snake_id,
    CoreDuelRootProfileResult* out_result
) {
    if (board == NULL || snake_id == NULL || out_result == NULL || board->snake_count != 2) {
        return CORE_ERROR;
    }
    memset(out_result, 0, sizeof(*out_result));
    const Snake* snake = BoardFindSnakeConst(board, snake_id);
    if (snake == NULL || snake->body_len == 0) {
        return CORE_ERROR;
    }
    const char* opponent_id = NULL;
    int snake_index = -1;
    int opponent_index = -1;
    for (int i = 0; i < board->snake_count; i++) {
        if (strcmp(board->snakes[i].id, snake_id) == 0) {
            snake_index = i;
        } else {
            opponent_id = board->snakes[i].id;
            opponent_index = i;
        }
    }
    if (snake_index < 0 || opponent_id == NULL || opponent_index < 0) {
        return CORE_ERROR;
    }

    MoveDirection opponent_moves[4];
    int opponent_move_count = core_command_moves(board, opponent_id, opponent_moves);
    if (opponent_move_count <= 0) {
        return CORE_ERROR;
    }
    for (int i = 0; i < opponent_move_count; i++) {
        out_result->opponent_command_mask |= (uint8_t)(1u << opponent_moves[i]);
    }

    MoveDirection safe_moves[4];
    int safe_count = BoardSafeMoves(board, snake_id, safe_moves);
    uint8_t safe_mask = 0;
    for (int i = 0; i < safe_count; i++) {
        safe_mask |= (uint8_t)(1u << safe_moves[i]);
    }

    CoreSearchState state;
    if (!CoreSearchStateInit(&state, board)) {
        return CORE_ERROR;
    }
    const char* ids[2] = {snake_id, opponent_id};
    MoveDirection moves[2];
    for (int own_move = MOVE_UP; own_move <= MOVE_RIGHT; own_move++) {
        CoreDuelRootCommandProfile* command = &out_result->commands[own_move];
        command->evaluated = true;
        command->safe_by_board_rules = (safe_mask & (1u << own_move)) != 0;
        moves[0] = (MoveDirection)own_move;
        for (int reply = 0; reply < opponent_move_count; reply++) {
            moves[1] = opponent_moves[reply];
            uint32_t causes[2] = {0, 0};
            if (!CoreSearchStateMakeMovesDetailed(&state, ids, moves, 2, causes, 2)) {
                CoreSearchStateFree(&state);
                return CORE_ERROR;
            }
            const Board* child = CoreSearchStateBoard(&state);
            CoreOutcome outcome = CoreClassifyDuelOutcome(child, snake_id, opponent_id);
            uint8_t reply_bit = (uint8_t)(1u << opponent_moves[reply]);
            command->opponent_reply_mask |= reply_bit;
            command->immediate_causes |= causes[snake_index];
            if (outcome == CORE_OUTCOME_WIN) {
                command->win_reply_mask |= reply_bit;
            } else if (outcome == CORE_OUTCOME_DRAW) {
                command->draw_reply_mask |= reply_bit;
            } else if (outcome == CORE_OUTCOME_LOSS) {
                command->loss_reply_mask |= reply_bit;
            } else {
                command->both_alive_reply_mask |= reply_bit;
            }
            if (!CoreSearchStateUnmake(&state)) {
                CoreSearchStateFree(&state);
                return CORE_ERROR;
            }
        }
        command->alive_reply_mask = command->win_reply_mask | command->both_alive_reply_mask;
        command->alive_reply_count = core_popcount4(command->alive_reply_mask);
    }
    CoreSearchStateFree(&state);
    return CORE_OK;
}

static bool core_relaxed_root_state(
    const Board* board,
    const char* snake_id,
    MoveDirection root_move,
    CoreSearchState* out_state,
    int* out_post_move_length,
    uint32_t* out_cause
) {
    const Snake* snake = BoardFindSnakeConst(board, snake_id);
    if (snake == NULL || snake->body_len == 0) {
        return false;
    }
    Board* relaxed = BoardCreate(board->width, board->height, "standard", 0);
    if (relaxed == NULL || !BoardAddSnake(relaxed, snake)) {
        BoardFree(relaxed);
        return false;
    }
    relaxed->snakes[0].health = INT_MAX / 4;
    Coord destination = MoveStep(SnakeHead(snake), root_move);
    bool eats = core_coord_in_array(board->food, board->food_count, destination);
    if (eats && !BoardAddFood(relaxed, destination)) {
        BoardFree(relaxed);
        return false;
    }
    *out_post_move_length = core_snake_length(snake) + (eats ? 1 : 0);
    if (!CoreSearchStateInit(out_state, relaxed)) {
        BoardFree(relaxed);
        return false;
    }
    BoardFree(relaxed);
    const char* ids[1] = {snake_id};
    MoveDirection moves[1] = {root_move};
    uint32_t causes[1] = {0};
    if (!CoreSearchStateMakeMovesDetailed(out_state, ids, moves, 1, causes, 1)) {
        CoreSearchStateFree(out_state);
        return false;
    }
    out_state->board.food_count = 0;
    *out_cause = causes[0];
    const Snake* moved = BoardFindSnakeConst(CoreSearchStateBoard(out_state), snake_id);
    if (moved != NULL) {
        *out_post_move_length = core_snake_length(moved);
    }
    return true;
}

typedef struct {
    size_t cell_count;
    unsigned char* blocked;
    unsigned char* visited;
    unsigned char* degree;
    int* queue;
} CoreRelaxedCapacityWorkspace;

static void core_relaxed_capacity_workspace_free(CoreRelaxedCapacityWorkspace* workspace) {
    free(workspace->blocked);
    free(workspace->visited);
    free(workspace->degree);
    free(workspace->queue);
    memset(workspace, 0, sizeof(*workspace));
}

static bool core_relaxed_capacity_workspace_init(
    CoreRelaxedCapacityWorkspace* workspace,
    size_t cell_count
) {
    if (workspace->cell_count == cell_count && workspace->blocked != NULL &&
        workspace->visited != NULL && workspace->degree != NULL && workspace->queue != NULL) {
        return true;
    }
    core_relaxed_capacity_workspace_free(workspace);
    workspace->blocked = (unsigned char*)malloc(cell_count * sizeof(unsigned char));
    workspace->visited = (unsigned char*)malloc(cell_count * sizeof(unsigned char));
    workspace->degree = (unsigned char*)malloc(cell_count * sizeof(unsigned char));
    workspace->queue = (int*)malloc(cell_count * sizeof(int));
    if (workspace->blocked == NULL || workspace->visited == NULL ||
        workspace->degree == NULL || workspace->queue == NULL) {
        core_relaxed_capacity_workspace_free(workspace);
        return false;
    }
    workspace->cell_count = cell_count;
    return true;
}

static int core_relaxed_static_capacity_with_workspace(
    const Board* board,
    const char* snake_id,
    bool* out_head_in_cyclic_core,
    CoreRelaxedCapacityWorkspace* workspace
) {
    if (out_head_in_cyclic_core != NULL) {
        *out_head_in_cyclic_core = false;
    }
    const Snake* snake = BoardFindSnakeConst(board, snake_id);
    size_t cell_count = 0;
    if (snake == NULL || snake->body_len == 0 || !core_cell_count(board, &cell_count)) {
        return 0;
    }
    CoreRelaxedCapacityWorkspace local_workspace = {0};
    CoreRelaxedCapacityWorkspace* active_workspace = workspace != NULL
        ? workspace
        : &local_workspace;
    if (!core_relaxed_capacity_workspace_init(active_workspace, cell_count)) {
        return -1;
    }
    unsigned char* blocked = active_workspace->blocked;
    unsigned char* visited = active_workspace->visited;
    unsigned char* degree = active_workspace->degree;
    int* queue = active_workspace->queue;
    memset(blocked, 0, cell_count * sizeof(unsigned char));
    memset(visited, 0, cell_count * sizeof(unsigned char));
    for (int i = 0; i < snake->body_len; i++) {
        if (BoardInBounds(board, snake->body[i])) {
            blocked[core_coord_index(board, snake->body[i])] = 1;
        }
    }
    Coord head = SnakeHead(snake);
    int start = core_coord_index(board, head);
    blocked[start] = 0;
    visited[start] = 1;
    queue[0] = start;
    int read = 0;
    int write = 1;
    while (read < write) {
        int index = queue[read++];
        Coord coord = {index % board->width, index / board->width};
        for (int move = MOVE_UP; move <= MOVE_RIGHT; move++) {
            Coord next = MoveStep(coord, (MoveDirection)move);
            if (!BoardInBounds(board, next)) {
                continue;
            }
            int next_index = core_coord_index(board, next);
            if (!blocked[next_index] && !visited[next_index]) {
                visited[next_index] = 1;
                queue[write++] = next_index;
            }
        }
    }
    if (out_head_in_cyclic_core != NULL) {
        /* The connected component may only be cyclic beyond a narrow doorway.
         * Peel its acyclic fringe so opponent closure stays enforced until the
         * head actually enters the cyclic core through an uncontested edge. */
        memset(degree, 0, cell_count * sizeof(unsigned char));
        int peel_read = 0;
        int peel_write = 0;
        for (size_t raw_index = 0; raw_index < cell_count; raw_index++) {
            if (!visited[raw_index]) {
                continue;
            }
            Coord coord = {(int)(raw_index % (size_t)board->width),
                           (int)(raw_index / (size_t)board->width)};
            for (int move = MOVE_UP; move <= MOVE_RIGHT; move++) {
                Coord next = MoveStep(coord, (MoveDirection)move);
                if (BoardInBounds(board, next) && visited[core_coord_index(board, next)]) {
                    degree[raw_index]++;
                }
            }
            if (degree[raw_index] < 2) {
                queue[peel_write++] = (int)raw_index;
                blocked[raw_index] = 2;
            }
        }
        while (peel_read < peel_write) {
            int index = queue[peel_read++];
            Coord coord = {index % board->width, index / board->width};
            for (int move = MOVE_UP; move <= MOVE_RIGHT; move++) {
                Coord next = MoveStep(coord, (MoveDirection)move);
                if (!BoardInBounds(board, next)) {
                    continue;
                }
                int next_index = core_coord_index(board, next);
                if (visited[next_index] && blocked[next_index] != 2 && degree[next_index] > 0) {
                    degree[next_index]--;
                    if (degree[next_index] < 2) {
                        blocked[next_index] = 2;
                        queue[peel_write++] = next_index;
                    }
                }
            }
        }
        *out_head_in_cyclic_core = blocked[start] != 2;
    }
    if (workspace == NULL) {
        core_relaxed_capacity_workspace_free(&local_workspace);
    }
    return write;
}

static int core_relaxed_static_capacity(
    const Board* board,
    const char* snake_id,
    bool* out_head_in_cyclic_core
) {
    return core_relaxed_static_capacity_with_workspace(
        board, snake_id, out_head_in_cyclic_core, NULL
    );
}

static bool core_body_seen(const Coord* seen, int seen_count, int body_len, const Snake* snake) {
    for (int state = 0; state < seen_count; state++) {
        if (memcmp(seen + (size_t)state * (size_t)body_len, snake->body, (size_t)body_len * sizeof(Coord)) == 0) {
            return true;
        }
    }
    return false;
}

typedef struct {
    const Board* board;
    const unsigned char* blocked;
    int* discovery;
    int* low;
    int* parent;
    int* edge_from;
    int* edge_to;
    int edge_count;
    int clock;
    int start;
    int best_capacity;
    uint8_t best_start_neighbor_mask;
    int start_component_count;
    unsigned int* component_marks;
    unsigned int component_stamp;
} CoreBiconnectedContext;

typedef struct {
    size_t cell_count;
    unsigned char* blocked;
    int* storage;
    unsigned int* marks;
} CoreBiconnectedWorkspace;

static void core_biconnected_workspace_free(CoreBiconnectedWorkspace* workspace) {
    free(workspace->blocked);
    free(workspace->storage);
    free(workspace->marks);
    memset(workspace, 0, sizeof(*workspace));
}

static bool core_biconnected_workspace_init(
    CoreBiconnectedWorkspace* workspace,
    size_t cell_count
) {
    if (workspace->cell_count == cell_count && workspace->blocked != NULL &&
        workspace->storage != NULL && workspace->marks != NULL) {
        return true;
    }
    core_biconnected_workspace_free(workspace);
    workspace->blocked = (unsigned char*)malloc(cell_count * sizeof(unsigned char));
    workspace->storage = (int*)malloc(cell_count * 7 * sizeof(int));
    workspace->marks = (unsigned int*)malloc(cell_count * sizeof(unsigned int));
    if (workspace->blocked == NULL || workspace->storage == NULL || workspace->marks == NULL) {
        core_biconnected_workspace_free(workspace);
        return false;
    }
    workspace->cell_count = cell_count;
    return true;
}

static void core_record_biconnected_component(
    CoreBiconnectedContext* context,
    int stop_from,
    int stop_to
) {
    context->component_stamp++;
    int capacity = 0;
    bool contains_start = false;
    while (context->edge_count > 0) {
        context->edge_count--;
        int from = context->edge_from[context->edge_count];
        int to = context->edge_to[context->edge_count];
        int vertices[2] = {from, to};
        for (int i = 0; i < 2; i++) {
            int vertex = vertices[i];
            if (context->component_marks[vertex] != context->component_stamp) {
                context->component_marks[vertex] = context->component_stamp;
                capacity++;
                contains_start = contains_start || vertex == context->start;
            }
        }
        if (from == stop_from && to == stop_to) {
            break;
        }
    }
    if (!contains_start || capacity <= context->best_capacity) {
        if (contains_start) {
            context->start_component_count++;
        }
        return;
    }
    context->start_component_count++;
    uint8_t neighbor_mask = 0;
    Coord start = {context->start % context->board->width,
                   context->start / context->board->width};
    for (int move = MOVE_UP; move <= MOVE_RIGHT; move++) {
        Coord neighbor = MoveStep(start, (MoveDirection)move);
        if (BoardInBounds(context->board, neighbor)) {
            int index = core_coord_index(context->board, neighbor);
            if (context->component_marks[index] == context->component_stamp) {
                neighbor_mask |= (uint8_t)(1u << move);
            }
        }
    }
    context->best_capacity = capacity;
    context->best_start_neighbor_mask = neighbor_mask;
}

static void core_biconnected_dfs(CoreBiconnectedContext* context, int vertex) {
    context->discovery[vertex] = ++context->clock;
    context->low[vertex] = context->discovery[vertex];
    Coord coord = {vertex % context->board->width, vertex / context->board->width};
    for (int move = MOVE_UP; move <= MOVE_RIGHT; move++) {
        Coord next = MoveStep(coord, (MoveDirection)move);
        if (!BoardInBounds(context->board, next)) {
            continue;
        }
        int child = core_coord_index(context->board, next);
        if (context->blocked[child]) {
            continue;
        }
        if (context->discovery[child] == 0) {
            context->parent[child] = vertex;
            context->edge_from[context->edge_count] = vertex;
            context->edge_to[context->edge_count++] = child;
            core_biconnected_dfs(context, child);
            if (context->low[child] < context->low[vertex]) {
                context->low[vertex] = context->low[child];
            }
            if (context->low[child] >= context->discovery[vertex]) {
                core_record_biconnected_component(context, vertex, child);
            }
        } else if (child != context->parent[vertex] &&
                   context->discovery[child] < context->discovery[vertex]) {
            context->edge_from[context->edge_count] = vertex;
            context->edge_to[context->edge_count++] = child;
            if (context->discovery[child] < context->low[vertex]) {
                context->low[vertex] = context->discovery[child];
            }
        }
    }
}

static int core_head_biconnected_capacity(
    const Board* board,
    const char* snake_id,
    uint8_t* out_neighbor_mask,
    bool* out_head_is_articulation,
    CoreBiconnectedWorkspace* workspace
) {
    *out_neighbor_mask = 0;
    *out_head_is_articulation = false;
    const Snake* snake = BoardFindSnakeConst(board, snake_id);
    size_t cell_count = 0;
    if (snake == NULL || snake->body_len == 0 || !core_cell_count(board, &cell_count) ||
        cell_count > SIZE_MAX / (7 * sizeof(int))) {
        return 0;
    }
    if (!core_biconnected_workspace_init(workspace, cell_count)) {
        return -1;
    }
    unsigned char* blocked = workspace->blocked;
    int* storage = workspace->storage;
    unsigned int* marks = workspace->marks;
    memset(blocked, 0, cell_count * sizeof(unsigned char));
    memset(storage, 0, cell_count * 7 * sizeof(int));
    memset(marks, 0, cell_count * sizeof(unsigned int));
    for (int snake_index = 0; snake_index < board->snake_count; snake_index++) {
        const Snake* occupied = &board->snakes[snake_index];
        for (int body = 0; body < occupied->body_len; body++) {
            if (BoardInBounds(board, occupied->body[body])) {
                blocked[core_coord_index(board, occupied->body[body])] = 1;
            }
        }
    }
    int start = core_coord_index(board, SnakeHead(snake));
    blocked[start] = 0;
    CoreBiconnectedContext context = {
        .board = board,
        .blocked = blocked,
        .discovery = storage,
        .low = storage + cell_count,
        .parent = storage + cell_count * 2,
        .edge_from = storage + cell_count * 3,
        .edge_to = storage + cell_count * 5,
        .edge_count = 0,
        .clock = 0,
        .start = start,
        .best_capacity = 0,
        .best_start_neighbor_mask = 0,
        .start_component_count = 0,
        .component_marks = marks,
        .component_stamp = 0,
    };
    for (size_t index = 0; index < cell_count; index++) {
        context.parent[index] = -1;
    }
    core_biconnected_dfs(&context, start);
    *out_neighbor_mask = context.best_start_neighbor_mask;
    *out_head_is_articulation = context.start_component_count > 1;
    int capacity = context.best_capacity;
    return capacity;
}

typedef struct {
    CoreStructuralProofResult result;
    CoreStructuralProofCutoff cutoff;
    int depth;
    int proved_capacity;
} CoreStructuralProofOutcome;

typedef struct {
    int* dangerous_arrival;
    int* opponent_vacate;
    unsigned char* shorter_reachability;
    size_t cell_count;
    int horizon;
    int shorter_equal_promotion_time;
    int shorter_body_promotion_time;
} CoreStructuralOpponentTiming;

static void core_structural_opponent_timing_free(CoreStructuralOpponentTiming* timing) {
    free(timing->dangerous_arrival);
    free(timing->shorter_reachability);
    memset(timing, 0, sizeof(*timing));
}

static bool core_structural_opponent_closes_at(
    const CoreStructuralOpponentTiming* timing,
    int index,
    int own_arrival,
    int dangerous_deadline
) {
    /* Equal-or-longer heads contest on the same turn. A shorter head loses
     * that collision, but if it occupied the cell one turn earlier its neck
     * can still occupy the doorway when we arrive. Original body cells use
     * their earliest truthful vacate turn independently of head reachability. */
    bool promoted_head_can_contest = own_arrival >= timing->shorter_equal_promotion_time &&
        own_arrival <= timing->horizon && timing->shorter_reachability != NULL &&
        timing->shorter_reachability[
            (size_t)own_arrival * timing->cell_count + (size_t)index
        ];
    int occupancy_arrival = own_arrival - 1;
    bool shorter_can_occupy = timing->shorter_reachability != NULL &&
        occupancy_arrival >= timing->shorter_body_promotion_time &&
        occupancy_arrival >= 0 && occupancy_arrival <= timing->horizon &&
        timing->shorter_reachability[
            (size_t)occupancy_arrival * timing->cell_count + (size_t)index
        ];
    return timing->dangerous_arrival[index] <= dangerous_deadline || promoted_head_can_contest ||
           shorter_can_occupy ||
           timing->opponent_vacate[index] > own_arrival;
}

typedef struct {
    const char* snake_id;
    const CoreSearchTimer* timer;
    CoreStructuralOpponentTiming opponent_timing;
    int body_len;
    int horizon;
    int ancestor_capacity;
    bool require_capacity_sufficient;
    uint64_t max_states;
    bool use_opponent_closure;
    Coord* ancestor_bodies;
    uint64_t explored_states;
    uint64_t* analysis_nodes;
    CoreRelaxedCapacityWorkspace capacity_workspace;
    CoreBiconnectedWorkspace biconnected_workspace;
} CoreStructuralProofContext;

static CoreStructuralProofOutcome core_structural_outcome(
    CoreStructuralProofResult result,
    CoreStructuralProofCutoff cutoff,
    int depth,
    int proved_capacity
) {
    CoreStructuralProofOutcome outcome = {result, cutoff, depth, proved_capacity};
    return outcome;
}

static CoreStructuralProofOutcome core_prove_structural_node(
    CoreSearchState* state,
    int depth,
    int ancestor_count,
    int safe_steps_remaining,
    int minimum_capacity,
    bool region_established,
    int known_capacity,
    int known_cyclic_core,
    CoreStructuralProofContext* context
) {
    if (core_search_timed_out(context->timer)) {
        return core_structural_outcome(
            CORE_STRUCTURAL_PROOF_UNKNOWN, CORE_STRUCTURAL_CUTOFF_DEADLINE, depth, 0
        );
    }
    if (context->explored_states >= context->max_states) {
        return core_structural_outcome(
            CORE_STRUCTURAL_PROOF_UNKNOWN,
            CORE_STRUCTURAL_CUTOFF_RESOURCE_LIMIT,
            depth,
            0
        );
    }

    const Board* board = CoreSearchStateBoard(state);
    const Snake* snake = BoardFindSnakeConst(board, context->snake_id);
    if (snake == NULL || snake->body_len != context->body_len) {
        return core_structural_outcome(
            CORE_STRUCTURAL_PROOF_UNSAFE, CORE_STRUCTURAL_CUTOFF_DEAD_END, depth, 0
        );
    }
    context->explored_states++;
    (*context->analysis_nodes)++;

    bool head_in_cyclic_core = known_cyclic_core > 0;
    int capacity = known_capacity;
    if (capacity < 0 || known_cyclic_core < 0) {
        capacity = core_relaxed_static_capacity_with_workspace(
            board,
            context->snake_id,
            &head_in_cyclic_core,
            &context->capacity_workspace
        );
    }
    if (capacity < 0) {
        return core_structural_outcome(
            CORE_STRUCTURAL_PROOF_UNKNOWN, CORE_STRUCTURAL_CUTOFF_ALLOCATION_FAILURE, depth, 0
        );
    }
    bool capacity_sufficient = capacity >= context->body_len;
    bool was_region_established = region_established;
    if (capacity_sufficient && head_in_cyclic_core) {
        /* Static reachability may see through an opponent-controlled doorway.
         * Establish the region only from the actual occupied state, inside a
         * large enough biconnected block, with two legal next continuations
         * whose entry deadlines are still uncontested. */
        uint8_t component_neighbor_mask = 0;
        bool head_is_articulation = false;
        int component_capacity = core_head_biconnected_capacity(
            board,
            context->snake_id,
            &component_neighbor_mask,
            &head_is_articulation,
            &context->biconnected_workspace
        );
        if (component_capacity < 0) {
            return core_structural_outcome(
                CORE_STRUCTURAL_PROOF_UNKNOWN,
                CORE_STRUCTURAL_CUTOFF_ALLOCATION_FAILURE,
                depth,
                0
            );
        }
        if (component_capacity < context->body_len || head_is_articulation) {
            region_established = false;
        } else if (!region_established) {
            int uncontested_continuations = 0;
            Coord structural_head = SnakeHead(snake);
            const char* structural_ids[1] = {context->snake_id};
            for (int move = MOVE_UP; move <= MOVE_RIGHT; move++) {
                if ((component_neighbor_mask & (1u << move)) == 0) {
                    continue;
                }
                Coord destination = MoveStep(structural_head, (MoveDirection)move);
                int destination_index = core_coord_index(board, destination);
                if (!context->use_opponent_closure ||
                    !core_structural_opponent_closes_at(
                        &context->opponent_timing, destination_index, depth + 1, depth + 1
                    )) {
                    MoveDirection structural_moves[1] = {(MoveDirection)move};
                    if (!CoreSearchStateMakeMoves(state, structural_ids, structural_moves, 1)) {
                        return core_structural_outcome(
                            CORE_STRUCTURAL_PROOF_UNKNOWN,
                            CORE_STRUCTURAL_CUTOFF_ALLOCATION_FAILURE,
                            depth,
                            0
                        );
                    }
                    const Snake* continuation = BoardFindSnakeConst(
                        CoreSearchStateBoard(state), context->snake_id
                    );
                    if (continuation != NULL && continuation->body_len == context->body_len) {
                        uncontested_continuations++;
                    }
                    if (!CoreSearchStateUnmake(state)) {
                        return core_structural_outcome(
                            CORE_STRUCTURAL_PROOF_UNKNOWN,
                            CORE_STRUCTURAL_CUTOFF_ALLOCATION_FAILURE,
                            depth,
                            0
                        );
                    }
                }
            }
            region_established = uncontested_continuations >= 2;
        }
    } else {
        region_established = false;
    }
    if (was_region_established && !region_established && safe_steps_remaining >= 0) {
        safe_steps_remaining = -2;
    }
    if (capacity_sufficient) {
        if (context->require_capacity_sufficient && safe_steps_remaining == -1 &&
            region_established) {
            safe_steps_remaining = context->horizon;
            minimum_capacity = capacity;
        } else if (safe_steps_remaining >= 0 && capacity < minimum_capacity) {
            minimum_capacity = capacity;
        }
        if (context->require_capacity_sufficient && safe_steps_remaining == 0) {
            return core_structural_outcome(
                CORE_STRUCTURAL_PROOF_SAFE,
                CORE_STRUCTURAL_CUTOFF_HORIZON,
                depth,
                minimum_capacity
            );
        }
    } else {
        if (safe_steps_remaining >= 0) {
            safe_steps_remaining = -2;
        }
        minimum_capacity = 0;
    }

    bool exact_cycle = core_body_seen(
        context->ancestor_bodies, ancestor_count, context->body_len, snake
    );
    if (exact_cycle) {
        return core_structural_outcome(
            CORE_STRUCTURAL_PROOF_SAFE,
            CORE_STRUCTURAL_CUTOFF_CYCLE,
            depth,
            safe_steps_remaining >= 0 ? minimum_capacity : capacity
        );
    }
    if (ancestor_count >= context->ancestor_capacity) {
        return core_structural_outcome(
            CORE_STRUCTURAL_PROOF_UNKNOWN, CORE_STRUCTURAL_CUTOFF_HORIZON, depth, 0
        );
    }
    memcpy(
        context->ancestor_bodies + (size_t)ancestor_count * (size_t)context->body_len,
        snake->body,
        (size_t)context->body_len * sizeof(Coord)
    );

    const char* ids[1] = {context->snake_id};
    bool saw_unknown = false;
    CoreStructuralProofOutcome unknown = core_structural_outcome(
        CORE_STRUCTURAL_PROOF_UNKNOWN,
        CORE_STRUCTURAL_CUTOFF_NONE,
        depth,
        0
    );
    int deepest_dead_end = depth;
    typedef struct {
        MoveDirection move;
        int capacity;
        bool cyclic_core;
    } CoreStructuralMove;
    CoreStructuralMove ordered_moves[4];
    int ordered_count = 0;
    for (int move = MOVE_UP; move <= MOVE_RIGHT; move++) {
        Coord destination = MoveStep(SnakeHead(snake), (MoveDirection)move);
        if (!region_established && context->use_opponent_closure &&
            BoardInBounds(board, destination) &&
            core_structural_opponent_closes_at(
                &context->opponent_timing,
                core_coord_index(board, destination),
                depth + 1,
                depth + 1
            )) {
            continue;
        }
        MoveDirection moves[1] = {(MoveDirection)move};
        if (!CoreSearchStateMakeMoves(state, ids, moves, 1)) {
            return core_structural_outcome(
                CORE_STRUCTURAL_PROOF_UNKNOWN,
                CORE_STRUCTURAL_CUTOFF_ALLOCATION_FAILURE,
                depth,
                0
            );
        }
        const Snake* child = BoardFindSnakeConst(CoreSearchStateBoard(state), context->snake_id);
        if (child != NULL && child->body_len == context->body_len) {
            bool child_cyclic_core = false;
            int child_capacity = core_relaxed_static_capacity_with_workspace(
                CoreSearchStateBoard(state),
                context->snake_id,
                &child_cyclic_core,
                &context->capacity_workspace
            );
            if (child_capacity < 0) {
                (void)CoreSearchStateUnmake(state);
                return core_structural_outcome(
                    CORE_STRUCTURAL_PROOF_UNKNOWN,
                    CORE_STRUCTURAL_CUTOFF_ALLOCATION_FAILURE,
                    depth,
                    0
                );
            }
            int insert = ordered_count;
            while (insert > 0 &&
                   ((!ordered_moves[insert - 1].cyclic_core && child_cyclic_core) ||
                    (ordered_moves[insert - 1].cyclic_core == child_cyclic_core &&
                     ordered_moves[insert - 1].capacity < child_capacity))) {
                ordered_moves[insert] = ordered_moves[insert - 1];
                insert--;
            }
            ordered_moves[insert] = (CoreStructuralMove){
                (MoveDirection)move, child_capacity, child_cyclic_core
            };
            ordered_count++;
        }
        if (!CoreSearchStateUnmake(state)) {
            return core_structural_outcome(
                CORE_STRUCTURAL_PROOF_UNKNOWN,
                CORE_STRUCTURAL_CUTOFF_ALLOCATION_FAILURE,
                depth,
                0
            );
        }
    }
    for (int ordered = 0; ordered < ordered_count; ordered++) {
        MoveDirection move = ordered_moves[ordered].move;
        MoveDirection moves[1] = {move};
        if (!CoreSearchStateMakeMoves(state, ids, moves, 1)) {
            return core_structural_outcome(
                CORE_STRUCTURAL_PROOF_UNKNOWN,
                CORE_STRUCTURAL_CUTOFF_ALLOCATION_FAILURE,
                depth,
                0
            );
        }
        const Snake* child = BoardFindSnakeConst(CoreSearchStateBoard(state), context->snake_id);
        CoreStructuralProofOutcome child_outcome = core_structural_outcome(
            CORE_STRUCTURAL_PROOF_UNSAFE,
            CORE_STRUCTURAL_CUTOFF_DEAD_END,
            depth + 1,
            0
        );
        if (child != NULL && child->body_len == context->body_len) {
            child_outcome = core_prove_structural_node(
                state,
                depth + 1,
                ancestor_count + 1,
                safe_steps_remaining >= 0 ? safe_steps_remaining - 1 : safe_steps_remaining,
                minimum_capacity,
                region_established,
                ordered_moves[ordered].capacity,
                ordered_moves[ordered].cyclic_core ? 1 : 0,
                context
            );
        }
        if (!CoreSearchStateUnmake(state)) {
            return core_structural_outcome(
                CORE_STRUCTURAL_PROOF_UNKNOWN,
                CORE_STRUCTURAL_CUTOFF_ALLOCATION_FAILURE,
                depth,
                0
            );
        }
        if (child_outcome.result == CORE_STRUCTURAL_PROOF_SAFE) {
            return child_outcome;
        }
        if (child_outcome.result == CORE_STRUCTURAL_PROOF_UNKNOWN && !saw_unknown) {
            saw_unknown = true;
            unknown = child_outcome;
        } else if (child_outcome.result == CORE_STRUCTURAL_PROOF_UNSAFE && child_outcome.depth > deepest_dead_end) {
            deepest_dead_end = child_outcome.depth;
        }
    }
    if (saw_unknown) {
        return unknown;
    }
    return core_structural_outcome(
        CORE_STRUCTURAL_PROOF_UNSAFE,
        CORE_STRUCTURAL_CUTOFF_DEAD_END,
        deepest_dead_end,
        0
    );
}

static int core_compare_ints(const void* left, const void* right) {
    int left_value = *(const int*)left;
    int right_value = *(const int*)right;
    return (left_value > right_value) - (left_value < right_value);
}

static int core_space_time_saturating_add(int left, int right) {
    if (left >= CORE_SPACE_TIME_NEVER || right >= CORE_SPACE_TIME_NEVER ||
        right > CORE_SPACE_TIME_NEVER - left) {
        return CORE_SPACE_TIME_NEVER;
    }
    return left + right;
}

static int core_distinct_food_promotion_time(
    const int* sorted_food_arrivals,
    int reachable_food_count,
    int required_meals
) {
    if (required_meals <= 0) {
        return 0;
    }
    if (reachable_food_count < required_meals) {
        return CORE_SPACE_TIME_NEVER;
    }
    int arrival = sorted_food_arrivals[required_meals - 1];
    return arrival > required_meals ? arrival : required_meals;
}

static int core_food_delay_fixed_point(
    const int* sorted_food_arrivals,
    int reachable_food_count,
    int base_vacate
) {
    int delay = 0;
    while (delay < reachable_food_count) {
        int deadline = core_space_time_saturating_add(base_vacate, delay);
        int next_delay = 0;
        for (int i = 0; i < reachable_food_count; i++) {
            int meal_number = i + 1;
            int meal_time = sorted_food_arrivals[i] > meal_number
                ? sorted_food_arrivals[i]
                : meal_number;
            if (meal_time > deadline) {
                break;
            }
            next_delay++;
        }
        if (next_delay <= delay) {
            break;
        }
        delay = next_delay;
    }
    return delay;
}

static bool core_prepare_structural_food_timing(
    const Board* board,
    const char* snake_id,
    int own_length,
    int horizon,
    size_t cell_count,
    CoreStructuralOpponentTiming* timing,
    unsigned int* seen,
    size_t* queue,
    const CoreSearchTimer* timer
) {
    if (core_search_timed_out(timer)) {
        return false;
    }
    timing->shorter_equal_promotion_time = CORE_SPACE_TIME_NEVER;
    timing->shorter_body_promotion_time = CORE_SPACE_TIME_NEVER;
    if ((size_t)board->food_count > SIZE_MAX / sizeof(int)) {
        return false;
    }
    int* arrival = (int*)malloc(cell_count * sizeof(int));
    int* food_arrivals = board->food_count > 0
        ? (int*)malloc((size_t)board->food_count * sizeof(int))
        : NULL;
    /* Food timing is an optimistic lower bound: independent routes may share
     * cells and ignore temporary body occupancy. That can only promote or
     * retain an opponent too early, yielding UNKNOWN/UNSAFE rather than a
     * false SAFE certificate. Only distinct food present on this board and
     * reachable within the proof horizon contributes. */
    int* optimistic_vacate = (int*)calloc(cell_count, sizeof(int));
    if (arrival == NULL || optimistic_vacate == NULL ||
        (board->food_count > 0 && food_arrivals == NULL)) {
        free(arrival);
        free(food_arrivals);
        free(optimistic_vacate);
        return false;
    }
    if (core_search_timed_out(timer)) {
        free(arrival);
        free(food_arrivals);
        free(optimistic_vacate);
        return false;
    }

    bool okay = true;
    for (int i = 0; i < board->snake_count && okay; i++) {
        if (core_search_timed_out(timer)) {
            okay = false;
            break;
        }
        const Snake* snake = &board->snakes[i];
        int snake_length = core_snake_length(snake);
        if (snake->body_len <= 0 || strcmp(snake->id, snake_id) == 0) {
            continue;
        }
        unsigned int stamp = (unsigned int)i + 3u;
        if (stamp == 0) {
            okay = false;
            break;
        }
        okay = core_fill_opponent_arrival(
            board,
            snake_id,
            snake->id,
            0,
            INT_MAX,
            optimistic_vacate,
            arrival,
            NULL,
            seen,
            stamp,
            queue,
            cell_count,
            horizon,
            timer
        );
        if (!okay) {
            break;
        }
        int reachable_food_count = 0;
        for (int food_index = 0; food_index < board->food_count; food_index++) {
            if ((food_index & 63) == 0 && core_search_timed_out(timer)) {
                okay = false;
                break;
            }
            Coord food = board->food[food_index];
            if (!BoardInBounds(board, food)) {
                continue;
            }
            int food_time = arrival[core_coord_index(board, food)];
            if (food_time != CORE_SPACE_TIME_NEVER) {
                food_arrivals[reachable_food_count++] = food_time;
            }
        }
        if (!okay) {
            break;
        }
        if (reachable_food_count > 1) {
            qsort(
                food_arrivals,
                (size_t)reachable_food_count,
                sizeof(int),
                core_compare_ints
            );
        }

        if (snake_length < own_length) {
            int promotion_time = core_distinct_food_promotion_time(
                food_arrivals, reachable_food_count, own_length - snake_length
            );
            if (promotion_time < timing->shorter_equal_promotion_time) {
                timing->shorter_equal_promotion_time = promotion_time;
            }
            int body_promotion_time = snake_length >= 2
                ? 0
                : core_distinct_food_promotion_time(food_arrivals, reachable_food_count, 1);
            if (body_promotion_time < timing->shorter_body_promotion_time) {
                timing->shorter_body_promotion_time = body_promotion_time;
            }
        }

        if (core_is_constrictor(board)) {
            for (int body_index = 0; body_index < snake->body_len; body_index++) {
                Coord body = snake->body[body_index];
                if (BoardInBounds(board, body)) {
                    int index = core_coord_index(board, body);
                    timing->opponent_vacate[index] = CORE_SPACE_TIME_NEVER;
                }
            }
            continue;
        }
        for (int body_index = 0; body_index < snake->body_len; body_index++) {
            Coord body = snake->body[body_index];
            if (!BoardInBounds(board, body)) {
                continue;
            }
            int base_vacate = snake->body_len - body_index;
            int delay = core_food_delay_fixed_point(
                food_arrivals, reachable_food_count, base_vacate
            );
            int vacate_time = core_space_time_saturating_add(base_vacate, delay);
            int index = core_coord_index(board, body);
            if (vacate_time > timing->opponent_vacate[index]) {
                timing->opponent_vacate[index] = vacate_time;
            }
        }
    }
    free(arrival);
    free(food_arrivals);
    free(optimistic_vacate);
    return okay;
}

static bool core_prepare_structural_opponent_arrival(
    const Board* board,
    const char* snake_id,
    int own_length,
    int horizon,
    CoreStructuralOpponentTiming* out_timing,
    bool* out_considered,
    const CoreSearchTimer* timer
) {
    memset(out_timing, 0, sizeof(*out_timing));
    *out_considered = false;
    if (core_search_timed_out(timer)) {
        return false;
    }
    size_t cell_count = 0;
    if (!core_cell_count(board, &cell_count) || cell_count > SIZE_MAX / sizeof(int)) {
        return false;
    }
    if (cell_count > SIZE_MAX / (2 * sizeof(int))) {
        return false;
    }
    int* timing_storage = (int*)malloc(2 * cell_count * sizeof(int));
    if (timing_storage == NULL) {
        return false;
    }
    int* dangerous_arrival = timing_storage;
    int* opponent_vacate = timing_storage + cell_count;
    for (size_t i = 0; i < cell_count; i++) {
        dangerous_arrival[i] = CORE_SPACE_TIME_NEVER;
        opponent_vacate[i] = 0;
    }
    out_timing->dangerous_arrival = dangerous_arrival;
    out_timing->opponent_vacate = opponent_vacate;
    out_timing->cell_count = cell_count;
    out_timing->horizon = horizon;
    if (!core_has_opponent(board, snake_id)) {
        return true;
    }

    *out_considered = true;
    if ((size_t)horizon + 1 > SIZE_MAX / cell_count) {
        free(timing_storage);
        memset(out_timing, 0, sizeof(*out_timing));
        return false;
    }
    size_t state_count = ((size_t)horizon + 1) * cell_count;
    if (state_count > SIZE_MAX / sizeof(unsigned int) || state_count > SIZE_MAX / sizeof(size_t)) {
        free(timing_storage);
        memset(out_timing, 0, sizeof(*out_timing));
        return false;
    }
    int* optimistic_head_vacate = (int*)calloc(cell_count, sizeof(int));
    unsigned int* seen = (unsigned int*)calloc(state_count, sizeof(unsigned int));
    size_t* queue = (size_t*)malloc(state_count * sizeof(size_t));
    unsigned char* shorter_reachability = (unsigned char*)calloc(state_count, 1);
    if (optimistic_head_vacate == NULL || seen == NULL || queue == NULL ||
        shorter_reachability == NULL) {
        free(timing_storage);
        memset(out_timing, 0, sizeof(*out_timing));
        free(optimistic_head_vacate);
        free(seen);
        free(queue);
        free(shorter_reachability);
        return false;
    }
    if (core_search_timed_out(timer)) {
        free(timing_storage);
        free(optimistic_head_vacate);
        free(seen);
        free(queue);
        free(shorter_reachability);
        memset(out_timing, 0, sizeof(*out_timing));
        return false;
    }
    out_timing->shorter_reachability = shorter_reachability;
    bool food_timing_filled = core_prepare_structural_food_timing(
        board,
        snake_id,
        own_length,
        horizon,
        cell_count,
        out_timing,
        seen,
        queue,
        timer
    );
    /* Keep head reach and original-body occupancy as separate conservative
     * channels. Head reach ignores optional growth/body delay because an
     * adversary may skip food to arrive sooner. The maximal multi-food fixed
     * point above is used only by opponent_vacate. */
    bool dangerous_filled = food_timing_filled && core_fill_opponent_arrival(
        board,
        snake_id,
        NULL,
        own_length,
        INT_MAX,
        optimistic_head_vacate,
        dangerous_arrival,
        NULL,
        seen,
        1,
        queue,
        cell_count,
        horizon,
        timer
    );
    int* shorter_arrival = (int*)malloc(cell_count * sizeof(int));
    bool shorter_filled = food_timing_filled && shorter_arrival != NULL &&
        core_fill_opponent_arrival(
            board,
            snake_id,
            NULL,
            1,
            own_length,
            optimistic_head_vacate,
            shorter_arrival,
            shorter_reachability,
            seen,
            2,
            queue,
            cell_count,
            horizon,
            timer
    );
    free(optimistic_head_vacate);
    free(seen);
    free(queue);
    free(shorter_arrival);
    if (!food_timing_filled || !dangerous_filled || !shorter_filled) {
        free(timing_storage);
        free(shorter_reachability);
        memset(out_timing, 0, sizeof(*out_timing));
        return false;
    }
    return true;
}

static int core_rectangle_perimeter_index(
    Coord coord,
    int min_x,
    int min_y,
    int max_x,
    int max_y
) {
    int width = max_x - min_x + 1;
    int height = max_y - min_y + 1;
    if (coord.y == min_y && coord.x >= min_x && coord.x <= max_x) {
        return coord.x - min_x;
    }
    if (coord.x == max_x && coord.y > min_y && coord.y <= max_y) {
        return width - 1 + coord.y - min_y;
    }
    if (coord.y == max_y && coord.x >= min_x && coord.x < max_x) {
        return width + height - 2 + max_x - coord.x;
    }
    if (coord.x == min_x && coord.y > min_y && coord.y < max_y) {
        return 2 * width + height - 3 + max_y - coord.y;
    }
    return -1;
}

static Coord core_rectangle_perimeter_coord(
    int index,
    int min_x,
    int min_y,
    int max_x,
    int max_y
) {
    int width = max_x - min_x + 1;
    int height = max_y - min_y + 1;
    if (index < width) {
        return (Coord){min_x + index, min_y};
    }
    index -= width;
    if (index < height - 1) {
        return (Coord){max_x, min_y + 1 + index};
    }
    index -= height - 1;
    if (index < width - 1) {
        return (Coord){max_x - 1 - index, max_y};
    }
    index -= width - 1;
    return (Coord){min_x, max_y - 1 - index};
}

/* A snake occupying a proper contiguous segment of a rectangle perimeter can
 * sustain a self-cycle before any equal-or-longer opponent contests it. Here
 * CORE_SPACE_TIME_NEVER means "not reached within the bounded proof horizon",
 * not permanent opponent unreachability; later interaction remains minimax's
 * responsibility. */
static int core_structural_bounded_cycle_capacity(
    const Board* board,
    const Snake* snake,
    const CoreStructuralOpponentTiming* opponent_timing
) {
    if (snake == NULL || snake->body_len < 2) {
        return 0;
    }
    int min_x = snake->body[0].x;
    int max_x = min_x;
    int min_y = snake->body[0].y;
    int max_y = min_y;
    for (int i = 1; i < snake->body_len; i++) {
        Coord body = snake->body[i];
        if (body.x < min_x) min_x = body.x;
        if (body.x > max_x) max_x = body.x;
        if (body.y < min_y) min_y = body.y;
        if (body.y > max_y) max_y = body.y;
    }
    int width = max_x - min_x + 1;
    int height = max_y - min_y + 1;
    if (width < 2 || height < 2 || !BoardInBounds(board, (Coord){min_x, min_y}) ||
        !BoardInBounds(board, (Coord){max_x, max_y})) {
        return 0;
    }
    size_t capacity_size = 2 * (size_t)width + 2 * (size_t)height - 4;
    if (capacity_size > (size_t)INT_MAX || capacity_size <= (size_t)snake->body_len) {
        return 0;
    }
    int capacity = (int)capacity_size;
    int head_index = core_rectangle_perimeter_index(snake->body[0], min_x, min_y, max_x, max_y);
    int neck_index = core_rectangle_perimeter_index(snake->body[1], min_x, min_y, max_x, max_y);
    if (head_index < 0 || neck_index < 0) {
        return 0;
    }
    int body_step = (int)(((int64_t)neck_index - (int64_t)head_index + capacity) % capacity);
    if (body_step != 1 && body_step != capacity - 1) {
        return 0;
    }
    int expected = head_index;
    for (int i = 1; i < snake->body_len; i++) {
        int index = core_rectangle_perimeter_index(snake->body[i], min_x, min_y, max_x, max_y);
        expected = (expected + body_step) % capacity;
        if (index != expected) {
            return 0;
        }
    }
    for (int index = 0; index < capacity; index++) {
        Coord coord = core_rectangle_perimeter_coord(index, min_x, min_y, max_x, max_y);
        int forward_distance = body_step == 1
            ? (head_index - index + capacity) % capacity
            : (index - head_index + capacity) % capacity;
        int own_arrival = 1 + forward_distance;
        if (core_structural_opponent_closes_at(
                opponent_timing, core_coord_index(board, coord), own_arrival, capacity
            )) {
            return 0;
        }
    }
    return capacity;
}

/* Preserve the pre-structural-proof diagnostic contract while callers migrate
 * from CoreTrapStatus to CoreStructuralProofResult. This result must not be
 * used as evidence that a branching state is safe. */
static void core_analyze_legacy_self_trap(
    const Board* board,
    const char* snake_id,
    MoveDirection root_move,
    const CoreSearchTimer* timer,
    CoreRootCandidateStats* candidate,
    uint64_t* analysis_nodes
) {
    if (core_search_timed_out(timer)) {
        candidate->trap_status = CORE_TRAP_UNKNOWN;
        return;
    }
    CoreSearchState state;
    memset(&state, 0, sizeof(state));
    uint32_t root_cause = 0;
    int post_move_length = 0;
    if (!core_relaxed_root_state(board, snake_id, root_move, &state, &post_move_length, &root_cause)) {
        candidate->trap_status = CORE_TRAP_UNKNOWN;
        return;
    }
    candidate->post_move_length = post_move_length;
    const Snake* snake = BoardFindSnakeConst(CoreSearchStateBoard(&state), snake_id);
    if (snake == NULL || snake->body_len == 0) {
        candidate->trap_status = CORE_TRAP_IMMEDIATE_DEATH;
        candidate->trap_horizon = 0;
        candidate->immediate_causes |= root_cause;
        CoreSearchStateFree(&state);
        return;
    }
    candidate->relaxed_static_capacity = core_relaxed_static_capacity(
        CoreSearchStateBoard(&state), snake_id, NULL
    );
    if (core_search_timed_out(timer)) {
        candidate->trap_status = CORE_TRAP_UNKNOWN;
        CoreSearchStateFree(&state);
        return;
    }
    if (candidate->relaxed_static_capacity < 0) {
        candidate->trap_status = CORE_TRAP_UNKNOWN;
        CoreSearchStateFree(&state);
        return;
    }
    int proof_horizon = post_move_length - 1;
    int horizon = 1;
    candidate->trap_horizon = horizon;
    if (horizon >= proof_horizon) {
        candidate->trap_status = CORE_TRAP_SURVIVES_HORIZON;
        CoreSearchStateFree(&state);
        return;
    }
    Coord* seen = (Coord*)malloc((size_t)proof_horizon * (size_t)post_move_length * sizeof(Coord));
    if (seen == NULL) {
        candidate->trap_status = CORE_TRAP_UNKNOWN;
        CoreSearchStateFree(&state);
        return;
    }
    memcpy(seen, snake->body, (size_t)post_move_length * sizeof(Coord));
    int seen_count = 1;
    const char* ids[1] = {snake_id};
    while (horizon < proof_horizon) {
        if (core_search_timed_out(timer)) {
            candidate->trap_status = CORE_TRAP_UNKNOWN;
            break;
        }
        MoveDirection surviving_move = MOVE_INVALID;
        int surviving_count = 0;
        for (int move = MOVE_UP; move <= MOVE_RIGHT; move++) {
            MoveDirection moves[1] = {(MoveDirection)move};
            if (!CoreSearchStateMakeMoves(&state, ids, moves, 1)) {
                candidate->trap_status = CORE_TRAP_UNKNOWN;
                surviving_count = -1;
                break;
            }
            (*analysis_nodes)++;
            const Snake* child = BoardFindSnakeConst(CoreSearchStateBoard(&state), snake_id);
            if (child != NULL && child->body_len > 0) {
                surviving_move = (MoveDirection)move;
                surviving_count++;
            }
            if (!CoreSearchStateUnmake(&state)) {
                candidate->trap_status = CORE_TRAP_UNKNOWN;
                surviving_count = -1;
                break;
            }
        }
        if (surviving_count < 0) break;
        if (surviving_count == 0) {
            candidate->trap_status = CORE_TRAP_PROVEN_SELF_TRAP;
            candidate->trap_horizon = horizon;
            break;
        }
        if (surviving_count > 1) {
            candidate->trap_status = CORE_TRAP_OPEN_BRANCH;
            candidate->trap_horizon = horizon;
            break;
        }
        MoveDirection moves[1] = {surviving_move};
        if (!CoreSearchStateMakeMoves(&state, ids, moves, 1)) {
            candidate->trap_status = CORE_TRAP_UNKNOWN;
            break;
        }
        (*analysis_nodes)++;
        horizon++;
        candidate->trap_horizon = horizon;
        snake = BoardFindSnakeConst(CoreSearchStateBoard(&state), snake_id);
        if (snake == NULL || snake->body_len != post_move_length) {
            candidate->trap_status = CORE_TRAP_UNKNOWN;
            break;
        }
        if (core_body_seen(seen, seen_count, post_move_length, snake)) {
            candidate->trap_status = CORE_TRAP_SURVIVES_CYCLE;
            break;
        }
        memcpy(
            seen + (size_t)seen_count * (size_t)post_move_length,
            snake->body,
            (size_t)post_move_length * sizeof(Coord)
        );
        seen_count++;
        if (horizon >= proof_horizon) {
            candidate->trap_status = CORE_TRAP_SURVIVES_HORIZON;
            break;
        }
    }
    if (candidate->trap_status == CORE_TRAP_NOT_ANALYZED) {
        candidate->trap_status = CORE_TRAP_SURVIVES_HORIZON;
    }
    free(seen);
    CoreSearchStateFree(&state);
}

static void core_analyze_self_trap(
    const Board* board,
    const char* snake_id,
    MoveDirection root_move,
    const CoreSearchTimer* timer,
    CoreRootCandidateStats* candidate,
    uint64_t* analysis_nodes,
    bool skip_capacity_deficit_proof
) {
    if (core_search_timed_out(timer)) {
        candidate->structural_proof = CORE_STRUCTURAL_PROOF_UNKNOWN;
        candidate->proof_cutoff = CORE_STRUCTURAL_CUTOFF_DEADLINE;
        return;
    }
    CoreSearchState state;
    memset(&state, 0, sizeof(state));
    uint32_t root_cause = 0;
    int post_move_length = 0;
    if (!core_relaxed_root_state(
        board,
        snake_id,
        root_move,
        &state,
        &post_move_length,
        &root_cause
    )) {
        candidate->structural_proof = CORE_STRUCTURAL_PROOF_UNKNOWN;
        candidate->proof_cutoff = CORE_STRUCTURAL_CUTOFF_ALLOCATION_FAILURE;
        return;
    }
    candidate->post_move_length = post_move_length;
    size_t cell_count = 0;
    if (!core_cell_count(CoreSearchStateBoard(&state), &cell_count) ||
        cell_count > (size_t)(INT_MAX - post_move_length)) {
        candidate->structural_proof = CORE_STRUCTURAL_PROOF_UNKNOWN;
        candidate->proof_cutoff = CORE_STRUCTURAL_CUTOFF_RESOURCE_LIMIT;
        CoreSearchStateFree(&state);
        return;
    }
    int proof_horizon = post_move_length + (int)cell_count;
    candidate->proof_horizon = proof_horizon;
    const Snake* snake = BoardFindSnakeConst(CoreSearchStateBoard(&state), snake_id);
    if (snake == NULL || snake->body_len == 0) {
        candidate->structural_proof = CORE_STRUCTURAL_PROOF_UNSAFE;
        candidate->proof_cutoff = CORE_STRUCTURAL_CUTOFF_DEAD_END;
        candidate->immediate_causes |= root_cause;
        CoreSearchStateFree(&state);
        return;
    }
    bool root_in_cyclic_core = false;
    candidate->relaxed_static_capacity = core_relaxed_static_capacity(
        CoreSearchStateBoard(&state), snake_id, &root_in_cyclic_core
    );
    if (core_search_timed_out(timer)) {
        candidate->structural_proof = CORE_STRUCTURAL_PROOF_UNKNOWN;
        candidate->proof_cutoff = CORE_STRUCTURAL_CUTOFF_DEADLINE;
        CoreSearchStateFree(&state);
        return;
    }
    if (candidate->relaxed_static_capacity < 0) {
        candidate->structural_proof = CORE_STRUCTURAL_PROOF_UNKNOWN;
        candidate->proof_cutoff = CORE_STRUCTURAL_CUTOFF_ALLOCATION_FAILURE;
        CoreSearchStateFree(&state);
        return;
    }
    if (skip_capacity_deficit_proof &&
        candidate->relaxed_static_capacity < candidate->post_move_length) {
        /* A completed SAFE candidate is already sufficient for the only
         * policy comparison that can dominate this capacity-deficient root.
         * Do not spend the bounded proof phase trying to upgrade it: UNKNOWN
         * is the conservative result; minimax retains its scheduled search
         * interval. */
        candidate->structural_proof = CORE_STRUCTURAL_PROOF_UNKNOWN;
        candidate->proof_cutoff = CORE_STRUCTURAL_CUTOFF_POLICY_SUFFICIENT;
        CoreSearchStateFree(&state);
        return;
    }
    CoreStructuralOpponentTiming opponent_timing = {0};
    bool opponent_closure_considered = false;
    if (!core_prepare_structural_opponent_arrival(
        board,
        snake_id,
        post_move_length,
        proof_horizon,
        &opponent_timing,
        &opponent_closure_considered,
        timer
    )) {
        candidate->structural_proof = CORE_STRUCTURAL_PROOF_UNKNOWN;
        candidate->proof_cutoff = core_search_timed_out(timer)
            ? CORE_STRUCTURAL_CUTOFF_DEADLINE
            : CORE_STRUCTURAL_CUTOFF_ALLOCATION_FAILURE;
        CoreSearchStateFree(&state);
        return;
    }
    candidate->opponent_closure_considered = opponent_closure_considered;
    if (core_search_timed_out(timer)) {
        candidate->structural_proof = CORE_STRUCTURAL_PROOF_UNKNOWN;
        candidate->proof_cutoff = CORE_STRUCTURAL_CUTOFF_DEADLINE;
        core_structural_opponent_timing_free(&opponent_timing);
        CoreSearchStateFree(&state);
        return;
    }
    Coord root_destination = SnakeHead(snake);
    if (opponent_closure_considered && BoardInBounds(board, root_destination) &&
        core_structural_opponent_closes_at(
            &opponent_timing, core_coord_index(board, root_destination), 1, 1
        )) {
        candidate->structural_proof = CORE_STRUCTURAL_PROOF_UNSAFE;
        candidate->proof_cutoff = CORE_STRUCTURAL_CUTOFF_DEAD_END;
        core_structural_opponent_timing_free(&opponent_timing);
        CoreSearchStateFree(&state);
        return;
    }
    int structural_capacity = core_structural_bounded_cycle_capacity(
        CoreSearchStateBoard(&state),
        snake,
        &opponent_timing
    );
    if (core_search_timed_out(timer)) {
        candidate->structural_proof = CORE_STRUCTURAL_PROOF_UNKNOWN;
        candidate->proof_cutoff = CORE_STRUCTURAL_CUTOFF_DEADLINE;
        core_structural_opponent_timing_free(&opponent_timing);
        CoreSearchStateFree(&state);
        return;
    }
    if (structural_capacity > 0) {
        candidate->structural_proof = CORE_STRUCTURAL_PROOF_SAFE;
        candidate->proof_cutoff = CORE_STRUCTURAL_CUTOFF_CAPACITY;
        candidate->structural_capacity = structural_capacity;
        candidate->explored_states = 1;
        (*analysis_nodes)++;
        core_structural_opponent_timing_free(&opponent_timing);
        CoreSearchStateFree(&state);
        return;
    }
    /* The short proof exists to distinguish a capacity-deficient dead pocket
     * from an unproved one. Capacity-sufficient roots go directly to the
     * longer certificate; replaying the same body-turnover prefix would spend
     * the bounded phase twice without adding evidence. */
    if (candidate->relaxed_static_capacity < post_move_length) {
        if ((size_t)post_move_length > SIZE_MAX / (size_t)post_move_length ||
            (size_t)post_move_length * (size_t)post_move_length > SIZE_MAX / sizeof(Coord)) {
            candidate->structural_proof = CORE_STRUCTURAL_PROOF_UNKNOWN;
            candidate->proof_cutoff = CORE_STRUCTURAL_CUTOFF_RESOURCE_LIMIT;
            core_structural_opponent_timing_free(&opponent_timing);
            CoreSearchStateFree(&state);
            return;
        }
        Coord* short_ancestors = (Coord*)malloc(
            (size_t)post_move_length * (size_t)post_move_length * sizeof(Coord)
        );
        if (short_ancestors == NULL) {
            candidate->structural_proof = CORE_STRUCTURAL_PROOF_UNKNOWN;
            candidate->proof_cutoff = CORE_STRUCTURAL_CUTOFF_ALLOCATION_FAILURE;
            core_structural_opponent_timing_free(&opponent_timing);
            CoreSearchStateFree(&state);
            return;
        }
        CoreStructuralProofContext short_context = {
            .snake_id = snake_id,
            .timer = timer,
            .opponent_timing = opponent_timing,
            .body_len = post_move_length,
            .horizon = post_move_length,
            .ancestor_capacity = post_move_length,
            .require_capacity_sufficient = false,
            .max_states = (uint64_t)post_move_length * (uint64_t)cell_count,
            .use_opponent_closure = opponent_closure_considered,
            .ancestor_bodies = short_ancestors,
            .explored_states = 0,
            .analysis_nodes = analysis_nodes,
        };
        CoreStructuralProofOutcome short_outcome = core_prove_structural_node(
            &state,
            1,
            0,
            -1,
            0,
            false,
            candidate->relaxed_static_capacity,
            root_in_cyclic_core ? 1 : 0,
            &short_context
        );
        core_relaxed_capacity_workspace_free(&short_context.capacity_workspace);
        core_biconnected_workspace_free(&short_context.biconnected_workspace);
        free(short_ancestors);
        if (short_outcome.result != CORE_STRUCTURAL_PROOF_UNKNOWN) {
            candidate->structural_proof = short_outcome.result;
            candidate->proof_cutoff = short_outcome.cutoff;
            candidate->explored_states = short_context.explored_states;
            candidate->structural_capacity = short_outcome.proved_capacity;
            core_structural_opponent_timing_free(&opponent_timing);
            CoreSearchStateFree(&state);
            return;
        }
        /* A static capacity jump caused by our tail vacating is not itself proof
         * that the head crossed a defensible doorway. Until such a doorway or an
         * exact uncontested cycle is certified, deficit roots remain UNKNOWN. */
        candidate->structural_proof = CORE_STRUCTURAL_PROOF_UNKNOWN;
        candidate->proof_cutoff = short_outcome.cutoff;
        candidate->explored_states = short_context.explored_states;
        core_structural_opponent_timing_free(&opponent_timing);
        CoreSearchStateFree(&state);
        return;
    }
    if (proof_horizon > (INT_MAX - 1) / 2) {
        candidate->structural_proof = CORE_STRUCTURAL_PROOF_UNKNOWN;
        candidate->proof_cutoff = CORE_STRUCTURAL_CUTOFF_RESOURCE_LIMIT;
        core_structural_opponent_timing_free(&opponent_timing);
        CoreSearchStateFree(&state);
        return;
    }
    int ancestor_capacity = proof_horizon * 2 + 1;
    if ((size_t)ancestor_capacity > SIZE_MAX / (size_t)post_move_length ||
        (size_t)ancestor_capacity * (size_t)post_move_length > SIZE_MAX / sizeof(Coord)) {
        candidate->structural_proof = CORE_STRUCTURAL_PROOF_UNKNOWN;
        candidate->proof_cutoff = CORE_STRUCTURAL_CUTOFF_RESOURCE_LIMIT;
        core_structural_opponent_timing_free(&opponent_timing);
        CoreSearchStateFree(&state);
        return;
    }
    Coord* ancestor_bodies = (Coord*)malloc(
        (size_t)ancestor_capacity * (size_t)post_move_length * sizeof(Coord)
    );
    if (ancestor_bodies == NULL) {
        candidate->structural_proof = CORE_STRUCTURAL_PROOF_UNKNOWN;
        candidate->proof_cutoff = CORE_STRUCTURAL_CUTOFF_ALLOCATION_FAILURE;
        core_structural_opponent_timing_free(&opponent_timing);
        CoreSearchStateFree(&state);
        return;
    }
    CoreStructuralProofContext context = {
        .snake_id = snake_id,
        .timer = timer,
        .opponent_timing = opponent_timing,
        .body_len = post_move_length,
        .horizon = proof_horizon,
        .ancestor_capacity = ancestor_capacity,
        .require_capacity_sufficient = true,
        /* Board-derived work guard: explore at most one full body turnover
         * plus one board traversal's worth of state layers. Every node may
         * run full-board capacity/topology analysis, so exhaustion remains
         * UNKNOWN instead of consuming the entire root-search deadline. */
        .max_states = ((uint64_t)post_move_length + (uint64_t)board->width +
                       (uint64_t)board->height) * (uint64_t)cell_count,
        .use_opponent_closure = opponent_closure_considered,
        .ancestor_bodies = ancestor_bodies,
        .explored_states = 0,
        .analysis_nodes = analysis_nodes,
    };
    CoreStructuralProofOutcome outcome = core_prove_structural_node(
        &state,
        1,
        0,
        candidate->relaxed_static_capacity < post_move_length ? -2 : -1,
        0,
        false,
        candidate->relaxed_static_capacity,
        root_in_cyclic_core ? 1 : 0,
        &context
    );
    core_relaxed_capacity_workspace_free(&context.capacity_workspace);
    core_biconnected_workspace_free(&context.biconnected_workspace);
    candidate->structural_proof = outcome.result;
    candidate->proof_cutoff = outcome.cutoff;
    candidate->explored_states = context.explored_states;
    candidate->structural_capacity = outcome.proved_capacity;
    free(ancestor_bodies);
    core_structural_opponent_timing_free(&opponent_timing);
    CoreSearchStateFree(&state);
}

typedef enum {
    CORE_FORCE_LOSS_PROVEN = 0,
    CORE_FORCE_LOSS_NOT_PROVEN = 1,
    CORE_FORCE_LOSS_UNKNOWN = 2,
} CoreForceLossResult;

#define CORE_REFUTATION_NODE_CAP 200000u

static CoreForceLossResult core_forced_loss_node(
    CoreSearchState* state,
    const char* snake_id,
    const char* opponent_id,
    int depth_remaining,
    const CoreSearchTimer* timer,
    uint64_t* nodes
) {
    const Board* board = CoreSearchStateBoard(state);
    CoreOutcome outcome = CoreClassifyDuelOutcome(board, snake_id, opponent_id);
    if (outcome == CORE_OUTCOME_LOSS) {
        return CORE_FORCE_LOSS_PROVEN;
    }
    if (outcome == CORE_OUTCOME_WIN || outcome == CORE_OUTCOME_DRAW) {
        return CORE_FORCE_LOSS_NOT_PROVEN;
    }
    if (depth_remaining <= 0) {
        return CORE_FORCE_LOSS_UNKNOWN;
    }
    if (*nodes >= CORE_REFUTATION_NODE_CAP || core_search_timed_out(timer)) {
        return CORE_FORCE_LOSS_UNKNOWN;
    }

    MoveDirection opponent_moves[4];
    int opponent_count = core_command_moves(board, opponent_id, opponent_moves);
    if (opponent_count <= 0) {
        return CORE_FORCE_LOSS_UNKNOWN;
    }
    const char* ids[2] = {snake_id, opponent_id};
    bool any_unknown_own_move = false;
    for (int own_move = MOVE_UP; own_move <= MOVE_RIGHT; own_move++) {
        bool move_refuted = false;
        bool move_unknown = false;
        for (int reply = 0; reply < opponent_count; reply++) {
            MoveDirection moves[2] = {(MoveDirection)own_move, opponent_moves[reply]};
            if (!CoreSearchStateMakeMoves(state, ids, moves, 2)) {
                return CORE_FORCE_LOSS_UNKNOWN;
            }
            (*nodes)++;
            CoreForceLossResult child = core_forced_loss_node(
                state,
                snake_id,
                opponent_id,
                depth_remaining - 1,
                timer,
                nodes
            );
            if (!CoreSearchStateUnmake(state)) {
                return CORE_FORCE_LOSS_UNKNOWN;
            }
            if (child == CORE_FORCE_LOSS_PROVEN) {
                move_refuted = true;
                break;
            }
            if (child == CORE_FORCE_LOSS_UNKNOWN) {
                move_unknown = true;
            }
        }
        if (!move_refuted && !move_unknown) {
            return CORE_FORCE_LOSS_NOT_PROVEN;
        }
        if (!move_refuted) {
            any_unknown_own_move = true;
        }
    }
    return any_unknown_own_move ? CORE_FORCE_LOSS_UNKNOWN : CORE_FORCE_LOSS_PROVEN;
}

static CoreRefutationStatus core_refute_root_move(
    const Board* board,
    const char* snake_id,
    const char* opponent_id,
    MoveDirection root_move,
    int trap_horizon,
    const CoreSearchTimer* timer,
    uint64_t* nodes
) {
    MoveDirection opponent_moves[4];
    int opponent_count = core_command_moves(board, opponent_id, opponent_moves);
    if (opponent_count <= 0) {
        return CORE_REFUTATION_UNKNOWN;
    }
    CoreSearchState state;
    if (!CoreSearchStateInit(&state, board)) {
        return CORE_REFUTATION_UNKNOWN;
    }
    const char* ids[2] = {snake_id, opponent_id};
    bool saw_unknown = false;
    for (int reply = 0; reply < opponent_count; reply++) {
        MoveDirection moves[2] = {root_move, opponent_moves[reply]};
        if (!CoreSearchStateMakeMoves(&state, ids, moves, 2)) {
            CoreSearchStateFree(&state);
            return CORE_REFUTATION_UNKNOWN;
        }
        (*nodes)++;
        CoreForceLossResult result = core_forced_loss_node(
            &state,
            snake_id,
            opponent_id,
            trap_horizon,
            timer,
            nodes
        );
        if (!CoreSearchStateUnmake(&state)) {
            CoreSearchStateFree(&state);
            return CORE_REFUTATION_UNKNOWN;
        }
        if (result == CORE_FORCE_LOSS_PROVEN) {
            CoreSearchStateFree(&state);
            return CORE_REFUTATION_PROVEN_REFUTABLE;
        }
        if (result == CORE_FORCE_LOSS_UNKNOWN) {
            saw_unknown = true;
        }
    }
    CoreSearchStateFree(&state);
    return saw_unknown ? CORE_REFUTATION_UNKNOWN : CORE_REFUTATION_NOT_REFUTABLE;
}

static bool core_structural_alternative(
    const CoreSearchStats* stats,
    uint8_t allowed_mask,
    MoveDirection excluded_move
) {
    for (int move = MOVE_UP; move <= MOVE_RIGHT; move++) {
        if (move == excluded_move || (allowed_mask & (1u << move)) == 0) {
            continue;
        }
        const CoreRootCandidateStats* candidate = &stats->root_candidates[move];
        if (candidate->alive_reply_count > 0 &&
            candidate->structural_proof == CORE_STRUCTURAL_PROOF_SAFE) {
            return true;
        }
    }
    return false;
}

typedef enum {
    CORE_ROOT_REPLY_CLOSURE_UNKNOWN = -1,
    CORE_ROOT_REPLY_CLOSURE_OPEN = 0,
    CORE_ROOT_REPLY_CLOSURE_CLOSED = 1,
} CoreRootReplyClosureResult;

static CoreRootReplyClosureResult core_root_reply_closes_all_continuations(
    const Board* board,
    const char* snake_id,
    const char* opponent_id,
    MoveDirection root_move,
    uint8_t both_alive_reply_mask
) {
    if (both_alive_reply_mask == 0) {
        return CORE_ROOT_REPLY_CLOSURE_OPEN;
    }
    CoreSearchState state;
    if (!CoreSearchStateInit(&state, board)) {
        return CORE_ROOT_REPLY_CLOSURE_UNKNOWN;
    }
    const char* ids[2] = {snake_id, opponent_id};
    for (int reply = MOVE_UP; reply <= MOVE_RIGHT; reply++) {
        if ((both_alive_reply_mask & (1u << reply)) == 0) {
            continue;
        }
        MoveDirection moves[2] = {root_move, (MoveDirection)reply};
        if (!CoreSearchStateMakeMoves(&state, ids, moves, 2)) {
            CoreSearchStateFree(&state);
            return CORE_ROOT_REPLY_CLOSURE_UNKNOWN;
        }
        MoveDirection continuations[4];
        int continuation_count = BoardSafeMoves(
            CoreSearchStateBoard(&state), snake_id, continuations
        );
        if (!CoreSearchStateUnmake(&state)) {
            CoreSearchStateFree(&state);
            return CORE_ROOT_REPLY_CLOSURE_UNKNOWN;
        }
        if (continuation_count == 0) {
            CoreSearchStateFree(&state);
            return CORE_ROOT_REPLY_CLOSURE_CLOSED;
        }
    }
    CoreSearchStateFree(&state);
    return CORE_ROOT_REPLY_CLOSURE_OPEN;
}

static bool core_is_guaranteed_terminal_non_loss(
    const CoreRootCandidateStats* candidate,
    uint8_t complete_opponent_reply_mask
) {
    uint8_t terminal_non_loss_mask = candidate->win_reply_mask | candidate->draw_reply_mask;
    return complete_opponent_reply_mask != 0 &&
        candidate->opponent_reply_mask == complete_opponent_reply_mask &&
        candidate->opponent_reply_mask == terminal_non_loss_mask &&
        candidate->both_alive_reply_mask == 0 && candidate->loss_reply_mask == 0;
}

static CoreStatus core_prepare_root_policy(
    const Board* board,
    const char* snake_id,
    CoreRootPolicy requested_policy,
    const CoreSearchTimer* timer,
    bool allow_policy_sufficient_cutoff,
    uint8_t strict_mask,
    CoreSearchStats* stats,
    uint8_t* out_allowed_mask
) {
    struct timespec start;
    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    *out_allowed_mask = strict_mask;
    bool standard_duel = board->snake_count == 2 && board->ruleset_name != NULL &&
        strcmp(board->ruleset_name, "standard") == 0;
    CoreRootPolicy effective_policy = standard_duel ? requested_policy : CORE_ROOT_POLICY_STRICT_MINIMAX;
    stats->root_policy_applied = effective_policy;
    if (!standard_duel) {
        stats->root_allowed_mask = strict_mask;
        for (int move = MOVE_UP; move <= MOVE_RIGHT; move++) {
            stats->root_candidates[move].allowed = (strict_mask & (1u << move)) != 0;
        }
        return CORE_OK;
    }

    CoreDuelRootProfileResult profile;
    CoreStatus profile_status = CoreDuelRootProfile(board, snake_id, &profile);
    if (profile_status != CORE_OK) {
        return profile_status;
    }
    const char* opponent_id = NULL;
    for (int i = 0; i < board->snake_count; i++) {
        if (strcmp(board->snakes[i].id, snake_id) != 0) {
            opponent_id = board->snakes[i].id;
            break;
        }
    }
    if (opponent_id == NULL) {
        return CORE_ERROR;
    }
    for (int move = MOVE_UP; move <= MOVE_RIGHT; move++) {
        CoreRootCandidateStats* candidate = &stats->root_candidates[move];
        const CoreDuelRootCommandProfile* command = &profile.commands[move];
        candidate->evaluated = command->evaluated;
        candidate->safe_by_board_rules = command->safe_by_board_rules;
        candidate->opponent_reply_mask = command->opponent_reply_mask;
        candidate->win_reply_mask = command->win_reply_mask;
        candidate->draw_reply_mask = command->draw_reply_mask;
        candidate->both_alive_reply_mask = command->both_alive_reply_mask;
        candidate->loss_reply_mask = command->loss_reply_mask;
        candidate->alive_reply_mask = command->alive_reply_mask;
        candidate->alive_reply_count = command->alive_reply_count;
        candidate->immediate_causes = command->immediate_causes;
        core_analyze_legacy_self_trap(
            board,
            snake_id,
            (MoveDirection)move,
            timer,
            candidate,
            &stats->root_analysis_nodes
        );
    }

    /* Analyze roots most likely to establish a positive certificate first.
     * This is proof scheduling, not a decision heuristic: all policy ordering
     * still uses the resulting SAFE/UNSAFE/UNKNOWN lattice. */
    int analysis_order[4] = {MOVE_UP, MOVE_DOWN, MOVE_LEFT, MOVE_RIGHT};
    for (int index = 1; index < 4; index++) {
        int move = analysis_order[index];
        const CoreRootCandidateStats* candidate = &stats->root_candidates[move];
        bool capacity_sufficient =
            candidate->relaxed_static_capacity >= candidate->post_move_length;
        int insert = index;
        while (insert > 0) {
            const CoreRootCandidateStats* previous =
                &stats->root_candidates[analysis_order[insert - 1]];
            bool previous_sufficient =
                previous->relaxed_static_capacity >= previous->post_move_length;
            if (previous_sufficient != capacity_sufficient) {
                if (previous_sufficient) {
                    break;
                }
            } else if (previous->relaxed_static_capacity >=
                       candidate->relaxed_static_capacity) {
                break;
            }
            analysis_order[insert] = analysis_order[insert - 1];
            insert--;
        }
        analysis_order[insert] = move;
    }
    bool safe_alive_certificate_found = false;
    for (int index = 0; index < 4; index++) {
        int move = analysis_order[index];
        CoreRootCandidateStats* candidate = &stats->root_candidates[move];
        const CoreDuelRootCommandProfile* command = &profile.commands[move];
        core_analyze_self_trap(
            board,
            snake_id,
            (MoveDirection)move,
            timer,
            candidate,
            &stats->root_analysis_nodes,
            safe_alive_certificate_found && allow_policy_sufficient_cutoff &&
                effective_policy == CORE_ROOT_POLICY_STANDARD_LADDER_OPPORTUNITY
        );
        CoreRootReplyClosureResult root_reply_closure =
            core_root_reply_closes_all_continuations(
            board,
            snake_id,
            opponent_id,
            (MoveDirection)move,
            command->both_alive_reply_mask
        );
        if (root_reply_closure == CORE_ROOT_REPLY_CLOSURE_CLOSED) {
            candidate->structural_proof = CORE_STRUCTURAL_PROOF_UNSAFE;
            candidate->proof_cutoff = CORE_STRUCTURAL_CUTOFF_DEAD_END;
            candidate->opponent_closure_considered = true;
        } else if (root_reply_closure == CORE_ROOT_REPLY_CLOSURE_UNKNOWN) {
            candidate->structural_proof = CORE_STRUCTURAL_PROOF_UNKNOWN;
            candidate->proof_cutoff = CORE_STRUCTURAL_CUTOFF_ALLOCATION_FAILURE;
        }
        if (candidate->alive_reply_count > 0 &&
            candidate->structural_proof == CORE_STRUCTURAL_PROOF_SAFE) {
            safe_alive_certificate_found = true;
        }
    }

    uint8_t allowed_mask = strict_mask;
    if (effective_policy == CORE_ROOT_POLICY_STANDARD_LADDER_OPPORTUNITY) {
        allowed_mask = 0x0f;
        bool any_surviving_reply = false;
        for (int move = MOVE_UP; move <= MOVE_RIGHT; move++) {
            if (stats->root_candidates[move].alive_reply_count > 0) {
                any_surviving_reply = true;
            }
        }
        if (any_surviving_reply) {
            for (int move = MOVE_UP; move <= MOVE_RIGHT; move++) {
                CoreRootCandidateStats* candidate = &stats->root_candidates[move];
                if (candidate->alive_reply_count == 0 && candidate->loss_reply_mask != 0) {
                    allowed_mask &= (uint8_t)~(1u << move);
                    candidate->rejection_reason = CORE_ROOT_REJECTION_NO_SURVIVING_REPLY;
                }
            }
        }

        for (int move = MOVE_UP; move <= MOVE_RIGHT; move++) {
            CoreRootCandidateStats* candidate = &stats->root_candidates[move];
            if ((allowed_mask & (1u << move)) == 0 ||
                candidate->trap_status != CORE_TRAP_PROVEN_SELF_TRAP ||
                candidate->trap_horizon >= candidate->post_move_length ||
                candidate->relaxed_static_capacity >= candidate->post_move_length ||
                !core_structural_alternative(stats, allowed_mask, (MoveDirection)move)) {
                continue;
            }
            candidate->refutation_status = core_refute_root_move(
                board,
                snake_id,
                opponent_id,
                (MoveDirection)move,
                candidate->trap_horizon,
                timer,
                &stats->root_analysis_nodes
            );
            if (candidate->refutation_status == CORE_REFUTATION_PROVEN_REFUTABLE) {
                allowed_mask &= (uint8_t)~(1u << move);
                candidate->rejection_reason = CORE_ROOT_REJECTION_PROVEN_SHORT_SELF_TRAP;
            }
        }

        bool has_safe_alive_alternative = false;
        for (int move = MOVE_UP; move <= MOVE_RIGHT; move++) {
            const CoreRootCandidateStats* candidate = &stats->root_candidates[move];
            if ((allowed_mask & (1u << move)) != 0 && candidate->alive_reply_count > 0 &&
                candidate->structural_proof == CORE_STRUCTURAL_PROOF_SAFE) {
                has_safe_alive_alternative = true;
                break;
            }
        }
        if (has_safe_alive_alternative) {
            for (int move = MOVE_UP; move <= MOVE_RIGHT; move++) {
                CoreRootCandidateStats* candidate = &stats->root_candidates[move];
                if ((allowed_mask & (1u << move)) == 0 ||
                    candidate->structural_proof == CORE_STRUCTURAL_PROOF_SAFE ||
                    core_is_guaranteed_terminal_non_loss(
                        candidate, profile.opponent_command_mask
                    ) ||
                    candidate->relaxed_static_capacity >= candidate->post_move_length) {
                    continue;
                }
                allowed_mask &= (uint8_t)~(1u << move);
                candidate->rejection_reason = CORE_ROOT_REJECTION_STRUCTURALLY_DOMINATED;
            }
        }
    }
    if (allowed_mask == 0) {
        allowed_mask = strict_mask != 0 ? strict_mask : 0x0f;
        for (int move = MOVE_UP; move <= MOVE_RIGHT; move++) {
            stats->root_candidates[move].rejection_reason = CORE_ROOT_REJECTION_NONE;
        }
    }
    for (int move = MOVE_UP; move <= MOVE_RIGHT; move++) {
        stats->root_candidates[move].allowed = (allowed_mask & (1u << move)) != 0;
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    stats->root_analysis_elapsed_ms = core_elapsed_ms(start, end);
    stats->root_allowed_mask = allowed_mask;
    *out_allowed_mask = allowed_mask;
    return CORE_OK;
}

static int core_reachable_after_move(
    const Board* board,
    const char* snake_id,
    MoveDirection move
) {
    const Snake* snake = BoardFindSnakeConst(board, snake_id);
    if (snake == NULL || snake->body_len == 0 || !core_valid_move_direction(move)) {
        return 0;
    }

    int reachable = 0;
    (void)CoreReachableSpace(board, MoveStep(SnakeHead(snake), move), snake_id, &reachable);
    return reachable;
}

static int core_tail_path_after_move(
    const Board* board,
    const char* snake_id,
    MoveDirection move
) {
    const Snake* snake = BoardFindSnakeConst(board, snake_id);
    if (snake == NULL || snake->body_len <= 1 || !core_valid_move_direction(move)) {
        return 0;
    }

    Coord* path = NULL;
    int path_count = 0;
    Coord start = MoveStep(SnakeHead(snake), move);
    Coord tail = snake->body[snake->body_len - 1];
    CoreStatus status = CoreShortestPath(board, start, tail, snake_id, &path, &path_count);
    free(path);
    if (status != CORE_OK || path_count <= 0) {
        return 0;
    }
    return path_count;
}

static MoveDirection core_current_heading(const Board* board, const char* snake_id) {
    const Snake* snake = BoardFindSnakeConst(board, snake_id);
    if (snake == NULL || snake->body_len < 2) {
        return MOVE_INVALID;
    }

    Coord head = SnakeHead(snake);
    Coord neck = snake->body[1];
    if (head.x == neck.x + 1 && head.y == neck.y) {
        return MOVE_RIGHT;
    }
    if (head.x == neck.x - 1 && head.y == neck.y) {
        return MOVE_LEFT;
    }
    if (head.x == neck.x && head.y == neck.y + 1) {
        return MOVE_UP;
    }
    if (head.x == neck.x && head.y == neck.y - 1) {
        return MOVE_DOWN;
    }
    return MOVE_INVALID;
}

static int core_cached_reachable_after_move(
    const Board* board,
    const char* snake_id,
    const MoveDirection* original_moves,
    int move_count,
    int* reachable_spaces,
    MoveDirection move
) {
    int index = core_find_move_index(original_moves, move_count, move);
    if (index == INT_MAX) {
        return 0;
    }
    if (reachable_spaces[index] < 0) {
        reachable_spaces[index] = core_reachable_after_move(board, snake_id, move);
    }
    return reachable_spaces[index];
}

typedef struct {
    int immediate_exits;
    int forced_steps;
    int reachable;
} CoreCorridorMoveMetrics;

static int core_open_neighbor_count(
    const Board* board,
    const unsigned char* blocked,
    int current,
    int previous,
    int* out_next
) {
    int count = 0;
    int next_index = -1;
    int x = current % board->width;
    int y = current / board->width;
    int neighbors[4];
    int neighbor_count = 0;

    if (y + 1 < board->height) {
        neighbors[neighbor_count++] = current + board->width;
    }
    if (y > 0) {
        neighbors[neighbor_count++] = current - board->width;
    }
    if (x > 0) {
        neighbors[neighbor_count++] = current - 1;
    }
    if (x + 1 < board->width) {
        neighbors[neighbor_count++] = current + 1;
    }

    for (int i = 0; i < neighbor_count; i++) {
        int neighbor = neighbors[i];
        if (neighbor == previous || blocked[neighbor]) {
            continue;
        }
        count++;
        next_index = neighbor;
    }

    if (out_next != NULL) {
        *out_next = next_index;
    }
    return count;
}

static bool core_corridor_metrics_after_move(
    const Board* board,
    const char* snake_id,
    MoveDirection move,
    CoreCorridorMoveMetrics* out_metrics
) {
    if (board == NULL || snake_id == NULL || out_metrics == NULL || !core_valid_move_direction(move)) {
        return false;
    }

    const Snake* snake = BoardFindSnakeConst(board, snake_id);
    size_t cell_count = 0;
    if (snake == NULL || snake->body_len == 0 || !core_cell_count(board, &cell_count)) {
        return false;
    }

    Coord head = SnakeHead(snake);
    Coord target = MoveStep(head, move);
    if (!BoardInBounds(board, target)) {
        return false;
    }

    unsigned char* blocked = (unsigned char*)calloc(cell_count, sizeof(unsigned char));
    if (blocked == NULL) {
        return false;
    }

    core_fill_movement_blocks(board, snake_id, blocked);
    int target_index = core_coord_index(board, target);
    if (blocked[target_index]) {
        free(blocked);
        return false;
    }

    int previous = core_coord_index(board, head);
    /* After taking the candidate move, the old head is the neck and cannot be
     * treated as an escape branch while walking the forced corridor. */
    blocked[previous] = 1;
    int current = target_index;
    int next = -1;
    int exits = core_open_neighbor_count(board, blocked, current, previous, &next);
    int immediate_exits = exits;
    int forced_steps = 0;
    int max_steps = core_snake_length(snake) + 4;
    while (exits == 1 && forced_steps < max_steps) {
        int old_current = current;
        current = next;
        previous = old_current;
        forced_steps++;
        exits = core_open_neighbor_count(board, blocked, current, previous, &next);
    }

    free(blocked);

    out_metrics->immediate_exits = immediate_exits;
    out_metrics->forced_steps = forced_steps;
    out_metrics->reachable = core_reachable_after_move(board, snake_id, move);
    return true;
}

static bool core_corridor_metrics_are_better(
    const CoreCorridorMoveMetrics* candidate,
    const CoreCorridorMoveMetrics* current
) {
    if (candidate->immediate_exits != current->immediate_exits) {
        return candidate->immediate_exits > current->immediate_exits;
    }
    if (candidate->forced_steps != current->forced_steps) {
        return candidate->forced_steps < current->forced_steps;
    }
    return candidate->reachable > current->reachable;
}

static bool core_constrained_root_corridor_move(
    const Board* board,
    const char* snake_id,
    const MoveDirection* moves,
    int move_count,
    MoveDirection* out_move
) {
    if (board == NULL || snake_id == NULL || moves == NULL || out_move == NULL || move_count < 2) {
        return false;
    }

    const Snake* snake = BoardFindSnakeConst(board, snake_id);
    if (snake == NULL || snake->body_len == 0 || core_snake_length(snake) < 12) {
        return false;
    }

    int current_reachable = 0;
    if (CoreReachableSpace(board, SnakeHead(snake), snake_id, &current_reachable) != CORE_OK) {
        return false;
    }
    if (current_reachable >= core_snake_length(snake)) {
        return false;
    }

    CoreCorridorMoveMetrics best_metrics = {0, 0, 0};
    MoveDirection best_move = MOVE_INVALID;
    bool has_best = false;
    bool has_forced_corridor = false;
    for (int i = 0; i < move_count; i++) {
        CoreCorridorMoveMetrics metrics;
        if (!core_corridor_metrics_after_move(board, snake_id, moves[i], &metrics)) {
            continue;
        }
        if (metrics.immediate_exits <= 1 && metrics.forced_steps >= 3 && metrics.reachable <= core_snake_length(snake)) {
            has_forced_corridor = true;
        }
        if (!has_best || core_corridor_metrics_are_better(&metrics, &best_metrics)) {
            has_best = true;
            best_metrics = metrics;
            best_move = moves[i];
        }
    }

    if (!has_best || !has_forced_corridor || best_metrics.immediate_exits < 2 || !core_valid_move_direction(best_move)) {
        return false;
    }

    *out_move = best_move;
    return true;
}

static bool core_equal_score_move_is_better(
    const Board* board,
    const char* snake_id,
    const MoveDirection* original_moves,
    int move_count,
    int* reachable_spaces,
    MoveDirection candidate,
    MoveDirection current_best,
    MoveDirection preferred,
    bool terminal_loss_tie
) {
    const Snake* snake = BoardFindSnakeConst(board, snake_id);
    size_t cell_count = board != NULL && board->width > 0 && board->height > 0 ?
        (size_t)board->width * (size_t)board->height :
        0;
    int length = snake != NULL ? core_snake_length(snake) : 0;
    bool constrained_endgame = length >= 12 && cell_count > 0 && (size_t)length * 3 >= cell_count;
    if (!constrained_endgame && terminal_loss_tie && snake != NULL && length >= 12) {
        int current_reachable = 0;
        if (CoreReachableSpace(board, SnakeHead(snake), snake_id, &current_reachable) == CORE_OK &&
            current_reachable < length) {
            constrained_endgame = true;
        }
    }
    if (constrained_endgame) {
        if (terminal_loss_tie) {
            CoreCorridorMoveMetrics candidate_metrics;
            CoreCorridorMoveMetrics current_best_metrics;
            if (core_corridor_metrics_after_move(board, snake_id, candidate, &candidate_metrics) &&
                core_corridor_metrics_after_move(board, snake_id, current_best, &current_best_metrics) &&
                (
                    candidate_metrics.immediate_exits != current_best_metrics.immediate_exits ||
                    candidate_metrics.forced_steps != current_best_metrics.forced_steps ||
                    candidate_metrics.reachable != current_best_metrics.reachable
                )) {
                return core_corridor_metrics_are_better(&candidate_metrics, &current_best_metrics);
            }
        }
        int candidate_tail_path = core_tail_path_after_move(board, snake_id, candidate);
        int current_best_tail_path = core_tail_path_after_move(board, snake_id, current_best);
        if (candidate_tail_path != current_best_tail_path) {
            return candidate_tail_path > current_best_tail_path;
        }
        if (terminal_loss_tie) {
            MoveDirection heading = core_current_heading(board, snake_id);
            if (core_valid_move_direction(heading)) {
                if (candidate == heading && current_best != heading) {
                    return true;
                }
                if (current_best == heading && candidate != heading) {
                    return false;
                }
            }
        }
    }

    bool preferred_valid = core_valid_move_direction(preferred);
    if (preferred_valid && candidate == preferred && current_best != preferred) {
        return true;
    }
    if (preferred_valid && current_best == preferred && candidate != preferred) {
        return false;
    }

    int candidate_space = core_cached_reachable_after_move(
        board,
        snake_id,
        original_moves,
        move_count,
        reachable_spaces,
        candidate
    );
    int current_best_space = core_cached_reachable_after_move(
        board,
        snake_id,
        original_moves,
        move_count,
        reachable_spaces,
        current_best
    );
    if (candidate_space != current_best_space) {
        return candidate_space > current_best_space;
    }

    return core_preferred_order_rank(original_moves, move_count, candidate, preferred) <
        core_preferred_order_rank(original_moves, move_count, current_best, preferred);
}

static uint32_t core_transition_cause(
    const char* snake_id,
    const char* opponent_id,
    const uint32_t* causes,
    int snake_index,
    int opponent_index,
    const Board* after
) {
    uint32_t own_cause = snake_index >= 0 ? causes[snake_index] : CORE_TERMINAL_CAUSE_NONE;
    uint32_t opponent_cause = opponent_index >= 0 ? causes[opponent_index] : CORE_TERMINAL_CAUSE_NONE;
    CoreOutcome outcome = CoreClassifyDuelOutcome(after, snake_id, opponent_id);
    if (outcome == CORE_OUTCOME_LOSS) {
        return own_cause;
    }
    if (outcome == CORE_OUTCOME_WIN) {
        return opponent_cause | CORE_TERMINAL_CAUSE_OPPONENT_ELIMINATED;
    }
    if (outcome == CORE_OUTCOME_DRAW) {
        return own_cause | opponent_cause;
    }
    return CORE_TERMINAL_CAUSE_NONE;
}

static CoreStatus core_minimax_search(
    const Board* board,
    const char* snake_id,
    int depth,
    int ply,
    double alpha,
    double beta,
    MoveDirection preferred_move,
    uint32_t incoming_cause,
    CoreSearchContext* context,
    bool* timed_out,
    CoreSearchValue* out_value,
    MoveDirection* out_best_move
) {
    if (core_search_timed_out(&context->timer)) {
        *timed_out = true;
        return CORE_OK;
    }
    CoreSearchStats* stats = context->stats;
    if (stats != NULL) {
        stats->nodes++;
    }

    CoreOutcome terminal_outcome = CoreClassifyDuelOutcome(board, snake_id, context->opponent_id);
    bool tt_node_enabled = context->tt_enabled && depth >= 2 && terminal_outcome == CORE_OUTCOME_UNRESOLVED;
    uint64_t hash = 0;
    double original_alpha = alpha;
    double original_beta = beta;
    MoveDirection tt_best_move = MOVE_INVALID;
    CoreTtBound tt_bound = CORE_TT_EXACT;
    if (tt_node_enabled) {
        hash = CoreZobristHashBoard(board);
        if (stats != NULL) {
            stats->tt_probes++;
        }
        CoreSearchValue tt_value = core_unresolved_value(0.0);
        bool tt_collision = false;
        CoreTtProbeResult tt_probe = CoreTtProbe(
            &context->tt,
            hash,
            depth,
            alpha,
            beta,
            &tt_value,
            &tt_best_move,
            &tt_bound,
            &tt_collision
        );
        if (stats != NULL && tt_collision) {
            stats->tt_collisions++;
        }
        if (tt_probe != CORE_TT_MISS) {
            if (stats != NULL) {
                stats->tt_hits++;
                if (tt_bound == CORE_TT_EXACT) {
                    stats->tt_exact_hits++;
                } else if (tt_bound == CORE_TT_LOWER) {
                    stats->tt_lower_hits++;
                } else if (tt_bound == CORE_TT_UPPER) {
                    stats->tt_upper_hits++;
                }
            }
        }
        if (tt_probe == CORE_TT_CUTOFF) {
            if (stats != NULL) {
                stats->tt_cutoffs++;
            }
            *out_value = tt_value;
            if (out_best_move != NULL) {
                *out_best_move = tt_best_move;
            }
            return CORE_OK;
        }
    }

    if (depth <= 0 || terminal_outcome != CORE_OUTCOME_UNRESOLVED) {
        if (stats != NULL) {
            stats->leaf_evals++;
        }
        CoreStatus status = CORE_OK;
        if (terminal_outcome != CORE_OUTCOME_UNRESOLVED) {
            *out_value = core_terminal_value(
                &context->config.weights,
                terminal_outcome,
                incoming_cause,
                0
            );
        } else {
            double score = 0.0;
            status = CoreEvaluateWithWeights(board, snake_id, &context->config.weights, &score);
            *out_value = core_unresolved_value(score);
        }
        if (status == CORE_OK && tt_node_enabled) {
            bool tt_collision = false;
            bool stored = CoreTtStore(&context->tt, hash, depth, *out_value, CORE_TT_EXACT, MOVE_INVALID, &tt_collision);
            if (stats != NULL && tt_collision) {
                stats->tt_collisions++;
            }
            if (stats != NULL && stored) {
                stats->tt_stores++;
            }
        }
        return status;
    }

    int snake_count = board->snake_count;
    const char** ids = CoreSearchWorkspaceSnakeIds(&context->workspace, ply);
    MoveDirection* moves = CoreSearchWorkspaceMoves(&context->workspace, ply);
    int* option_counts = CoreSearchWorkspaceOptionCounts(&context->workspace, ply);
    MoveDirection(*options)[4] = CoreSearchWorkspaceOptions(&context->workspace, ply);

    int own_index = -1;
    int opponent_index = -1;
    MoveDirection own_moves[4];
    MoveDirection original_own_moves[4];
    int own_move_count = 0;
    for (int i = 0; i < snake_count; i++) {
        ids[i] = board->snakes[i].id;
        if (strcmp(board->snakes[i].id, snake_id) == 0) {
            own_index = i;
            if (stats != NULL) {
                stats->safe_move_calls++;
            }
            if (ply == 0) {
                for (int move = MOVE_UP; move <= MOVE_RIGHT; move++) {
                    if ((context->root_allowed_mask & (1u << move)) != 0) {
                        own_moves[own_move_count++] = (MoveDirection)move;
                    }
                }
            } else {
                own_move_count = core_safe_moves_or_all(board, snake_id, own_moves);
            }
        } else {
            if (strcmp(board->snakes[i].id, context->opponent_id) == 0) {
                opponent_index = i;
            }
            /*
             * Opponent nodes must model legal commands, not our strategic
             * "safe move" filter. BoardSafeMoves rejects aggressive legal
             * moves such as stepping next to a longer head, which hides
             * corridor-blocking traps from the adversarial search.
             */
            option_counts[i] = core_command_moves(board, board->snakes[i].id, options[i]);
            if (option_counts[i] <= 0) {
                option_counts[i] = 1;
                options[i][0] = MOVE_INVALID;
            }
        }
    }

    if (own_index < 0 || own_move_count <= 0) {
        if (stats != NULL) {
            stats->leaf_evals++;
        }
        double score = 0.0;
        CoreStatus status = CoreEvaluateWithWeights(board, snake_id, &context->config.weights, &score);
        *out_value = core_unresolved_value(score);
        if (status == CORE_OK && tt_node_enabled) {
            bool tt_collision = false;
            bool stored = CoreTtStore(&context->tt, hash, depth, *out_value, CORE_TT_EXACT, MOVE_INVALID, &tt_collision);
            if (stats != NULL && tt_collision) {
                stats->tt_collisions++;
            }
            if (stats != NULL && stored) {
                stats->tt_stores++;
            }
        }
        return status;
    }

    int original_reachable_spaces[4];
    for (int i = 0; i < own_move_count; i++) {
        original_own_moves[i] = own_moves[i];
        original_reachable_spaces[i] = -1;
    }
    /* Preserve pre-sort order and lazily cache safety tie-break scores. */
    MoveDirection effective_preferred = core_valid_move_direction(tt_best_move) ? tt_best_move : preferred_move;
    MoveDirection tie_preferred = ply == 0 ? preferred_move : effective_preferred;
    core_order_moves(context, ply, own_moves, own_move_count, tt_best_move, preferred_move);
    CoreSearchValue best_value = core_unresolved_value(-DBL_MAX);
    MoveDirection best_move = own_moves[0];

    for (int order = 0; order < own_move_count; order++) {
        if (core_search_timed_out(&context->timer)) {
            *timed_out = true;
            break;
        }

        MoveDirection own_move = own_moves[order];
        option_counts[own_index] = 1;
        options[own_index][0] = own_move;

        int total = 1;
        for (int i = 0; i < snake_count; i++) {
            if (option_counts[i] <= 0 || total > INT_MAX / option_counts[i]) {
                return CORE_ERROR;
            }
            total *= option_counts[i];
        }

        CoreSearchValue worst_reply = core_unresolved_value(DBL_MAX);
        bool reply_cutoff = false;
        for (int combo = 0; combo < total; combo++) {
            if (core_search_timed_out(&context->timer)) {
                *timed_out = true;
                break;
            }

            int remainder = combo;
            for (int i = 0; i < snake_count; i++) {
                int option_index = remainder % option_counts[i];
                remainder /= option_counts[i];
                moves[i] = options[i][option_index];
            }

            CoreSearchValue value = core_unresolved_value(0.0);
            double child_beta = worst_reply.score < beta ? worst_reply.score : beta;
            uint32_t* transition_causes = CoreSearchWorkspaceTransitionCauses(&context->workspace, ply);
            memset(transition_causes, 0, (size_t)snake_count * sizeof(uint32_t));
            if (context->config.enable_make_unmake && context->state != NULL) {
                const Board* current = CoreSearchStateBoard(context->state);
                if (current == NULL || current->snake_count != snake_count) {
                    return CORE_ERROR;
                }
                for (int i = 0; i < snake_count; i++) {
                    ids[i] = current->snakes[i].id;
                }
                if (!CoreSearchStateMakeMovesDetailed(
                    context->state,
                    ids,
                    moves,
                    snake_count,
                    transition_causes,
                    snake_count
                )) {
                    return CORE_ERROR;
                }
                const Board* next = CoreSearchStateBoard(context->state);
                CoreStatus status = core_minimax_search(
                    next,
                    snake_id,
                    depth - 1,
                    ply + 1,
                    alpha,
                    child_beta,
                    MOVE_INVALID,
                    core_transition_cause(
                        snake_id,
                        context->opponent_id,
                        transition_causes,
                        own_index,
                        opponent_index,
                        next
                    ),
                    context,
                    timed_out,
                    &value,
                    NULL
                );
                if (!CoreSearchStateUnmake(context->state)) {
                    return CORE_ERROR;
                }
                if (status != CORE_OK || *timed_out) {
                    return status;
                }
            } else {
                if (stats != NULL) {
                    stats->clone_calls++;
                    stats->board_allocations++;
                }
                CoreSearchState cloned_state;
                memset(&cloned_state, 0, sizeof(cloned_state));
                if (!CoreSearchStateInit(&cloned_state, board) ||
                    !CoreSearchStateMakeMovesDetailed(
                        &cloned_state,
                        ids,
                        moves,
                        snake_count,
                        transition_causes,
                        snake_count
                    )) {
                    CoreSearchStateFree(&cloned_state);
                    return CORE_ERROR;
                }
                const Board* next = CoreSearchStateBoard(&cloned_state);
                CoreStatus status = core_minimax_search(
                    next,
                    snake_id,
                    depth - 1,
                    ply + 1,
                    alpha,
                    child_beta,
                    MOVE_INVALID,
                    core_transition_cause(
                        snake_id,
                        context->opponent_id,
                        transition_causes,
                        own_index,
                        opponent_index,
                        next
                    ),
                    context,
                    timed_out,
                    &value,
                    NULL
                );
                CoreSearchStateFree(&cloned_state);
                if (status != CORE_OK || *timed_out) {
                    return status;
                }
            }
            value = core_backup_child_value(&context->config.weights, value);

            if (value.score < worst_reply.score) {
                worst_reply = value;
            }
            if (worst_reply.score <= alpha) {
                reply_cutoff = true;
                if (stats != NULL) {
                    stats->beta_cutoffs++;
                    if (context->config.enable_move_ordering && order == 0) {
                        stats->move_order_first_choice_cutoffs++;
                    }
                }
                if (
                    context->config.enable_move_ordering &&
                    ply >= 0 &&
                    ply <= CORE_MINIMAX_MAX_DEPTH &&
                    own_move != context->killer_moves[ply][0]
                ) {
                    context->killer_moves[ply][1] = context->killer_moves[ply][0];
                    context->killer_moves[ply][0] = own_move;
                }
                if (context->config.enable_move_ordering && core_valid_move_direction(own_move)) {
                    context->history_scores[(int)own_move] += depth * depth;
                }
                break;
            }
        }

        if (*timed_out) {
            break;
        }

        if (ply == 0 && core_valid_move_direction(own_move)) {
            worst_reply.bound = reply_cutoff ? CORE_VALUE_BOUND_UPPER : CORE_VALUE_BOUND_EXACT;
            context->root_move_value_valid[(int)own_move] = true;
            context->root_move_values[(int)own_move] = worst_reply;
        }

        if (
            worst_reply.score > best_value.score ||
            (
                worst_reply.score == best_value.score &&
                core_equal_score_move_is_better(
                    board,
                    snake_id,
                    original_own_moves,
                    own_move_count,
                    original_reachable_spaces,
                    own_move,
                    best_move,
                    tie_preferred,
                    worst_reply.outcome == CORE_OUTCOME_LOSS
                )
            )
        ) {
            best_value = worst_reply;
            best_move = own_move;
        }
        if (best_value.score > alpha) {
            alpha = best_value.score;
        }
        if (ply == 0 && core_valid_move_direction(best_move)) {
            context->root_best_valid = true;
            context->root_best_move = best_move;
        }
        if (alpha >= beta) {
            break;
        }
    }

    if (*timed_out) {
        return CORE_OK;
    }

    CoreTtBound bound = CORE_TT_EXACT;
    if (best_value.score <= original_alpha) {
        bound = CORE_TT_UPPER;
    } else if (best_value.score >= original_beta) {
        bound = CORE_TT_LOWER;
    }
    best_value.bound = bound;
    *out_value = best_value;
    if (out_best_move != NULL) {
        *out_best_move = best_move;
    }
    if (context->config.enable_move_ordering && ply >= 0 && ply <= CORE_MINIMAX_MAX_DEPTH) {
        context->principal_variation[ply] = best_move;
    }
    if (tt_node_enabled) {
        bool tt_collision = false;
        bool stored = CoreTtStore(&context->tt, hash, depth, best_value, bound, best_move, &tt_collision);
        if (stats != NULL && tt_collision) {
            stats->tt_collisions++;
        }
        if (stats != NULL && stored) {
            stats->tt_stores++;
        }
    }
    return CORE_OK;
}

CoreStatus CoreMinimaxMoveWithStats(
    const Board* board,
    const char* snake_id,
    CoreSearchConfig config,
    MoveDirection* out_move,
    CoreSearchStats* out_stats
) {
    if (board == NULL || snake_id == NULL || out_move == NULL) {
        return CORE_ERROR;
    }
    if (config.fixed_depth < 0 || config.fixed_depth > CORE_MINIMAX_MAX_DEPTH) {
        return CORE_ERROR;
    }

    const Snake* snake = BoardFindSnakeConst(board, snake_id);
    if (snake == NULL || snake->body_len == 0) {
        return CORE_ERROR;
    }

    CoreSearchStats local_stats;
    CoreSearchStats* stats = out_stats != NULL ? out_stats : &local_stats;
    CoreSearchStatsInit(stats);
    stats->parallel_mode = config.parallel_mode;
    stats->parallel_workers_used = 1;

    MoveDirection safe_moves[4];
    stats->safe_move_calls++;
    int safe_move_count = core_safe_moves_or_all(board, snake_id, safe_moves);
    uint8_t root_allowed_mask = 0;
    for (int i = 0; i < safe_move_count; i++) {
        root_allowed_mask |= (uint8_t)(1u << safe_moves[i]);
    }
    MoveDirection completed_best = safe_moves[0];
    MoveDirection preferred_move = MOVE_INVALID;
    CoreSearchContext context;
    memset(&context, 0, sizeof(context));
    for (int depth = 0; depth <= CORE_MINIMAX_MAX_DEPTH; depth++) {
        context.principal_variation[depth] = MOVE_INVALID;
        context.killer_moves[depth][0] = MOVE_INVALID;
        context.killer_moves[depth][1] = MOVE_INVALID;
    }
    context.timer = core_search_timer_start(config.time_budget_ms);
    context.config = config;
    context.stats = stats;
    for (int i = 0; i < board->snake_count; i++) {
        if (strcmp(board->snakes[i].id, snake_id) != 0) {
            context.opponent_id = board->snakes[i].id;
            break;
        }
    }
    if (context.opponent_id == NULL) {
        context.opponent_id = "__missing_duel_opponent__";
    }
    /* Root safety and minimax are both required phases. The structural pass
     * gets one share and the primary search gets two: a duel root iteration
     * must evaluate both our command and the opponent reply layer. Incomplete
     * proofs fail conservatively as UNKNOWN and never borrow the scheduled
     * search interval. A noninterruptible leaf may still prevent a completed
     * depth or finish after the overall deadline. */
    int root_analysis_budget_ms = config.time_budget_ms / 3;
    if (root_analysis_budget_ms < 1) {
        root_analysis_budget_ms = 1;
    }
    stats->root_analysis_budget_ms = root_analysis_budget_ms;
    stats->search_reserved_ms = config.time_budget_ms > root_analysis_budget_ms
        ? config.time_budget_ms - root_analysis_budget_ms
        : 0;
    CoreSearchTimer root_analysis_timer = core_search_timer_prefix(
        &context.timer, root_analysis_budget_ms
    );
    CoreStatus policy_status = core_prepare_root_policy(
        board,
        snake_id,
        config.root_policy,
        &root_analysis_timer,
        config.fixed_depth == 0,
        root_allowed_mask,
        stats,
        &root_allowed_mask
    );
    if (policy_status != CORE_OK) {
        return policy_status;
    }
    context.root_allowed_mask = root_allowed_mask;
    for (int move = MOVE_UP; move <= MOVE_RIGHT; move++) {
        if ((root_allowed_mask & (1u << move)) != 0) {
            completed_best = (MoveDirection)move;
            break;
        }
    }
    context.root_best_valid = true;
    context.root_best_move = completed_best;
    bool fixed_depth_requested = config.fixed_depth > 0;
    int max_depth = fixed_depth_requested ? config.fixed_depth : CORE_MINIMAX_MAX_DEPTH;
    size_t tt_capacity = fixed_depth_requested ? (1u << 12) : (1u << 20);
    context.tt_enabled = config.enable_tt && CoreTtInit(&context.tt, tt_capacity);
    size_t cell_count = (size_t)board->width * (size_t)board->height;
    if (!CoreSearchWorkspaceInit(&context.workspace, board->snake_count, cell_count)) {
        CoreTtFree(&context.tt);
        return CORE_ERROR;
    }
    CoreSearchState state;
    memset(&state, 0, sizeof(state));
    if (config.enable_make_unmake) {
        if (!CoreSearchStateInit(&state, board)) {
            CoreSearchWorkspaceFree(&context.workspace);
            CoreTtFree(&context.tt);
            return CORE_ERROR;
        }
        /* The search state lives in this stack frame for the whole iterative
         * deepening loop; context.state must not outlive this function. */
        context.state = &state;
    }
    int completed_depth = 0;
    CoreSearchValue completed_value = core_unresolved_value(0.0);
    bool timed_out = false;
    bool completed_root_move_score_valid[4] = {false, false, false, false};
    CoreSearchValue completed_root_move_values[4];
    memset(completed_root_move_values, 0, sizeof(completed_root_move_values));
    struct timespec start;
    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int depth = 1; depth <= max_depth; depth++) {
        bool iteration_timed_out = false;
        CoreSearchValue value = core_unresolved_value(0.0);
        MoveDirection candidate = completed_best;
        stats->max_depth_started = depth;
        for (int move = MOVE_UP; move <= MOVE_RIGHT; move++) {
            context.root_move_value_valid[move] = false;
            context.root_move_values[move] = core_unresolved_value(0.0);
        }
        CoreStatus status = core_minimax_search(
            board,
            snake_id,
            depth,
            0,
            -DBL_MAX,
            DBL_MAX,
            preferred_move,
            CORE_TERMINAL_CAUSE_NONE,
            &context,
            &iteration_timed_out,
            &value,
            &candidate
        );
        if (status != CORE_OK) {
            if (context.state != NULL) {
                CoreSearchStateFree(context.state);
            }
            CoreSearchWorkspaceFree(&context.workspace);
            CoreTtFree(&context.tt);
            return status;
        }
        if (iteration_timed_out) {
            if (completed_depth == 0 && context.root_best_valid) {
                completed_best = context.root_best_move;
                if (context.root_move_value_valid[completed_best]) {
                    completed_value = context.root_move_values[completed_best];
                    for (int move = MOVE_UP; move <= MOVE_RIGHT; move++) {
                        completed_root_move_score_valid[move] = context.root_move_value_valid[move];
                        completed_root_move_values[move] = context.root_move_values[move];
                    }
                }
            }
            timed_out = true;
            break;
        }

        completed_best = candidate;
        context.root_best_valid = true;
        context.root_best_move = completed_best;
        preferred_move = candidate;
        if (context.config.enable_move_ordering) {
            context.principal_variation[0] = candidate;
        }
        completed_depth = depth;
        completed_value = value;
        for (int move = MOVE_UP; move <= MOVE_RIGHT; move++) {
            completed_root_move_score_valid[move] = context.root_move_value_valid[move];
            completed_root_move_values[move] = context.root_move_values[move];
        }
        if (!fixed_depth_requested && core_search_timed_out(&context.timer)) {
            break;
        }
        if (context.tt_enabled) {
            CoreTtNextGeneration(&context.tt);
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    stats->completed_depth = completed_depth;
    stats->timed_out = timed_out;
    if (completed_depth > 0) {
        stats->selection_reason = timed_out ?
            CORE_SELECTION_TIMEOUT_BEST_SO_FAR : CORE_SELECTION_MINIMAX;
    }
    stats->score = completed_value.score;
    stats->value = completed_value;
    for (int move = MOVE_UP; move <= MOVE_RIGHT; move++) {
        stats->root_move_score_valid[move] = completed_root_move_score_valid[move];
        stats->root_move_scores[move] = completed_root_move_values[move].score;
        stats->root_candidates[move].minimax_value_valid = completed_root_move_score_valid[move];
        stats->root_candidates[move].minimax_value = completed_root_move_values[move];
    }
    MoveDirection corridor_guard_move = MOVE_INVALID;
    bool completed_score_is_terminal_loss = completed_value.outcome == CORE_OUTCOME_LOSS;
    bool completed_score_is_terminal_win = completed_value.outcome == CORE_OUTCOME_WIN;
    if (
        !completed_score_is_terminal_win &&
        core_constrained_root_corridor_move(board, snake_id, safe_moves, safe_move_count, &corridor_guard_move) &&
        completed_root_move_score_valid[(int)corridor_guard_move] &&
        completed_root_move_values[(int)corridor_guard_move].bound == CORE_VALUE_BOUND_EXACT &&
        (root_allowed_mask & (1u << corridor_guard_move)) != 0
    ) {
        bool apply_corridor_guard = false;
        CoreSearchValue corridor_guard_value = completed_root_move_values[(int)corridor_guard_move];
        if (!completed_score_is_terminal_loss) {
            apply_corridor_guard = corridor_guard_value.outcome != CORE_OUTCOME_LOSS;
        } else {
            CoreCorridorMoveMetrics guard_metrics;
            CoreCorridorMoveMetrics completed_metrics;
            if (
                core_corridor_metrics_after_move(board, snake_id, corridor_guard_move, &guard_metrics) &&
                core_corridor_metrics_after_move(board, snake_id, completed_best, &completed_metrics) &&
                /*
                 * Compatibility guard for the issue-33/36 terminal-loss tie
                 * fixtures: prefer the corridor guard only when it opens a
                 * real side-exit choice over a much longer single-file line.
                 * These thresholds are regression-derived, not a complete
                 * territory model; revisit alongside issue #32 if a new
                 * terminal-band tie shape needs different geometry.
                 */
                completed_metrics.immediate_exits <= 1 &&
                guard_metrics.immediate_exits >= 2 &&
                completed_metrics.forced_steps - guard_metrics.forced_steps >= 4
            ) {
                apply_corridor_guard = true;
            }
        }

        /*
         * Dead-region leaves can enter the terminal-loss band before a true
         * terminal; keep the issue-33 long-corridor invariant without
         * overriding ordinary forced-loss survival ordering.
         */
        if (apply_corridor_guard) {
            completed_best = corridor_guard_move;
            completed_value = corridor_guard_value;
            stats->score = completed_value.score;
            stats->value = completed_value;
            stats->selection_reason = CORE_SELECTION_CORRIDOR_GUARD;
        }
    }
    stats->move = completed_best;
    stats->elapsed_ms = core_elapsed_ms(start, end);
    *out_move = completed_best;
    if (context.state != NULL) {
        CoreSearchStateFree(context.state);
    }
    CoreSearchWorkspaceFree(&context.workspace);
    CoreTtFree(&context.tt);
    return CORE_OK;
}

CoreStatus CoreMinimaxMove(
    const Board* board,
    const char* snake_id,
    int time_budget_ms,
    MoveDirection* out_move
) {
    CoreSearchConfig config = CoreSearchConfigDefault(time_budget_ms);
    CoreSearchStats stats;
    return CoreMinimaxMoveWithStats(board, snake_id, config, out_move, &stats);
}

CoreStatus CoreChokePoints(
    const Board* board,
    const char* snake_id,
    Coord** out_points,
    int* out_points_count
) {
    if (board == NULL || snake_id == NULL || out_points == NULL || out_points_count == NULL) {
        return CORE_ERROR;
    }

    *out_points = NULL;
    *out_points_count = 0;

    const Snake* snake = BoardFindSnakeConst(board, snake_id);
    if (snake == NULL || snake->body_len == 0) {
        return CORE_OK;
    }

    size_t cell_count = 0;
    if (!core_cell_count(board, &cell_count)) {
        return CORE_ERROR;
    }

    unsigned char* blocked = (unsigned char*)calloc(cell_count, sizeof(unsigned char));
    unsigned char* articulation = (unsigned char*)calloc(cell_count, sizeof(unsigned char));
    int* disc = (int*)calloc(cell_count, sizeof(int));
    int* low = (int*)calloc(cell_count, sizeof(int));
    int* parent = (int*)malloc(cell_count * sizeof(int));
    int* stack = (int*)malloc(cell_count * sizeof(int));
    int* child_count = (int*)calloc(cell_count, sizeof(int));
    int* next_dir = (int*)calloc(cell_count, sizeof(int));
    if (blocked == NULL || articulation == NULL || disc == NULL || low == NULL || parent == NULL || stack == NULL || child_count == NULL || next_dir == NULL) {
        free(blocked);
        free(articulation);
        free(disc);
        free(low);
        free(parent);
        free(stack);
        free(child_count);
        free(next_dir);
        return CORE_ERROR;
    }

    for (size_t i = 0; i < cell_count; i++) {
        parent[i] = -1;
    }
    core_fill_movement_blocks(board, snake_id, blocked);

    Coord start = SnakeHead(snake);
    int start_index = core_coord_index(board, start);
    int timer = 1;
    int stack_count = 1;
    stack[0] = start_index;
    disc[start_index] = timer;
    low[start_index] = timer;

    while (stack_count > 0) {
        int current = stack[stack_count - 1];
        int x = current % board->width;
        int y = current / board->width;
        int neighbors[4];
        int neighbor_count = 0;
        if (y + 1 < board->height) {
            neighbors[neighbor_count++] = current + board->width;
        }
        if (y > 0) {
            neighbors[neighbor_count++] = current - board->width;
        }
        if (x > 0) {
            neighbors[neighbor_count++] = current - 1;
        }
        if (x + 1 < board->width) {
            neighbors[neighbor_count++] = current + 1;
        }

        if (next_dir[current] < neighbor_count) {
            int neighbor = neighbors[next_dir[current]++];
            if (blocked[neighbor]) {
                continue;
            }
            if (disc[neighbor] == 0) {
                parent[neighbor] = current;
                child_count[current]++;
                disc[neighbor] = ++timer;
                low[neighbor] = disc[neighbor];
                stack[stack_count++] = neighbor;
            } else if (neighbor != parent[current] && disc[neighbor] < low[current]) {
                low[current] = disc[neighbor];
            }
            continue;
        }

        stack_count--;
        int p = parent[current];
        if (p >= 0) {
            if (low[current] < low[p]) {
                low[p] = low[current];
            }
            if (parent[p] >= 0 && low[current] >= disc[p]) {
                articulation[p] = 1;
            }
        }
    }

    /* The current head is the search entry point, not a choke point to return. */
    articulation[start_index] = 0;

    bool ok = core_collect_coords_from_marks(board, articulation, out_points, out_points_count);
    free(blocked);
    free(articulation);
    free(disc);
    free(low);
    free(parent);
    free(stack);
    free(child_count);
    free(next_dir);
    return ok ? CORE_OK : CORE_ERROR;
}

CoreStatus CoreEdgeTrapMove(
    const Board* board,
    const char* snake_id,
    bool* out_has_move,
    MoveDirection* out_move
) {
    if (board == NULL || snake_id == NULL || out_has_move == NULL || out_move == NULL) {
        return CORE_ERROR;
    }

    *out_has_move = false;
    *out_move = MOVE_INVALID;

    const Snake* snake = BoardFindSnakeConst(board, snake_id);
    if (snake == NULL || snake->body_len == 0) {
        return CORE_OK;
    }

    MoveDirection moves[4];
    int move_count = BoardSafeMoves(board, snake_id, moves);
    if (move_count == 0) {
        return CORE_OK;
    }

    Coord head = SnakeHead(snake);
    int own_length = core_snake_length(snake);
    double best_score = -DBL_MAX;
    MoveDirection best_move = MOVE_INVALID;

    for (int i = 0; i < move_count; i++) {
        Coord next = MoveStep(head, moves[i]);
        double move_score = -DBL_MAX;

        for (int j = 0; j < board->snake_count; j++) {
            const Snake* other = &board->snakes[j];
            if (strcmp(other->id, snake_id) == 0 || other->body_len == 0) {
                continue;
            }

            Coord other_head = SnakeHead(other);
            MoveDirection other_safe[4];
            int other_safe_count = BoardSafeMoves(board, other->id, other_safe);
            bool vulnerable = core_is_edge_cell(board, other_head) || other_safe_count <= 2 || core_snake_length(other) < own_length;
            if (!vulnerable) {
                continue;
            }

            int current_distance = core_manhattan(head, other_head);
            int next_distance = core_manhattan(next, other_head);
            if (next_distance > current_distance || next_distance > 3) {
                continue;
            }

            double candidate = 30.0 - (double)(next_distance * 8);
            if (core_is_edge_cell(board, other_head)) {
                candidate += 20.0;
            }
            if (other_safe_count <= 1) {
                candidate += 25.0;
            } else if (other_safe_count == 2) {
                candidate += 10.0;
            }
            if (core_snake_length(other) < own_length) {
                candidate += 15.0;
            }
            if (candidate > move_score) {
                move_score = candidate;
            }
        }

        if (move_score > best_score) {
            best_score = move_score;
            best_move = moves[i];
        }
    }

    if (best_move != MOVE_INVALID && best_score > 0.0) {
        *out_has_move = true;
        *out_move = best_move;
    }
    return CORE_OK;
}

CoreStatus CorePredictHazards(
    const Board* board,
    int turns_ahead,
    Coord** out_hazards,
    int* out_hazard_count
) {
    if (board == NULL || out_hazards == NULL || out_hazard_count == NULL) {
        return CORE_ERROR;
    }

    *out_hazards = NULL;
    *out_hazard_count = 0;

    size_t cell_count = 0;
    if (!core_cell_count(board, &cell_count)) {
        return CORE_ERROR;
    }

    unsigned char* predicted = (unsigned char*)calloc(cell_count, sizeof(unsigned char));
    if (predicted == NULL) {
        return CORE_ERROR;
    }

    for (int i = 0; i < board->hazard_count; i++) {
        Coord hazard = board->hazards[i];
        if (BoardInBounds(board, hazard)) {
            predicted[core_coord_index(board, hazard)] = 1;
        }
    }

    if (board->ruleset_name != NULL && strcmp(board->ruleset_name, "royale") == 0 && turns_ahead > 0) {
        int max_layers = board->width < board->height ? (board->width + 1) / 2 : (board->height + 1) / 2;
        int layers = turns_ahead < max_layers ? turns_ahead : max_layers;
        for (int layer = 0; layer < layers; layer++) {
            int min_x = layer;
            int min_y = layer;
            int max_x = board->width - 1 - layer;
            int max_y = board->height - 1 - layer;
            if (min_x > max_x || min_y > max_y) {
                break;
            }
            for (int x = min_x; x <= max_x; x++) {
                predicted[core_coord_index(board, (Coord){x, min_y})] = 1;
                predicted[core_coord_index(board, (Coord){x, max_y})] = 1;
            }
            for (int y = min_y; y <= max_y; y++) {
                predicted[core_coord_index(board, (Coord){min_x, y})] = 1;
                predicted[core_coord_index(board, (Coord){max_x, y})] = 1;
            }
        }
    }

    bool ok = core_collect_coords_from_marks(board, predicted, out_hazards, out_hazard_count);
    free(predicted);
    return ok ? CORE_OK : CORE_ERROR;
}

CoreStatus CoreEvaluateWithWeights(
    const Board* board,
    const char* snake_id,
    const CoreEvaluationWeights* weights,
    double* out_score
) {
    if (board == NULL || snake_id == NULL || weights == NULL || out_score == NULL) {
        return CORE_ERROR;
    }

    const Snake* snake = BoardFindSnakeConst(board, snake_id);
    if (snake == NULL || snake->body_len == 0) {
        *out_score = weights->terminal_loss;
        return CORE_OK;
    }

    if (board->snake_count == 1) {
        *out_score = weights->terminal_win;
        return CORE_OK;
    }

    Coord head = SnakeHead(snake);
    CoreSpaceTimeMetrics space_time;
    CoreStatus status = CoreSpaceTimeCompute(board, snake_id, &space_time);
    if (status != CORE_OK) {
        return status;
    }

    /* The terminal-loss-band shortcut is a duel leaf policy; FFA scoring keeps
     * its layered heuristic terms while still using time-aware reachable space. */
    if (board->snake_count == 2 && space_time.dead) {
        double step = core_terminal_survival_step(weights);
        int survivable = space_time.max_arrival;
        if (survivable > CORE_MINIMAX_MAX_DEPTH) {
            survivable = CORE_MINIMAX_MAX_DEPTH;
        }
        /* Compose dead-region leaves into the PR #31 survival-step terminal-loss band. */
        *out_score = weights->terminal_loss + step * (double)survivable;
        return CORE_OK;
    }

    int reachable = space_time.reachable_cells;

    MoveDirection safe_moves[4];
    int safe_count = BoardSafeMoves(board, snake_id, safe_moves);
    int length = core_snake_length(snake);
    double score = 0.0;

    score += weights->base;
    score += (double)snake->health * weights->health;
    score += (double)length * weights->length;
    score += (double)reachable * weights->reachable_space;
    score += (double)safe_count * weights->safe_moves;
    score += core_center_score(board, head) * weights->center;

    int food_distance = core_nearest_food_distance(board, head);
    if (food_distance != INT_MAX) {
        double food_weight = (double)snake->health < weights->low_health_threshold ?
            weights->low_health_food :
            weights->food;
        score += food_weight / (double)(food_distance + 1);
    }

    if (core_coord_in_array(board->hazards, board->hazard_count, head)) {
        score -= (double)board->hazard_damage * weights->hazard_damage + weights->hazard;
    }

    for (int i = 0; i < board->snake_count; i++) {
        const Snake* other = &board->snakes[i];
        if (strcmp(other->id, snake_id) == 0 || other->body_len == 0) {
            continue;
        }

        Coord other_head = SnakeHead(other);
        int other_length = core_snake_length(other);
        int distance = core_manhattan(head, other_head);
        score += (double)(length - other_length) * weights->length_advantage;
        if (distance == 1 && other_length >= length) {
            score -= weights->adjacent_equal_or_longer_penalty;
        } else if (distance == 1 && other_length < length) {
            score += weights->adjacent_shorter_bonus;
        }

        if (weights->opponent_reachable_space != 0.0 || weights->territory_delta != 0.0) {
            int other_reachable = 0;
            status = CoreReachableSpace(board, other_head, other->id, &other_reachable);
            if (status != CORE_OK) {
                return status;
            }
            score -= (double)other_reachable * weights->opponent_reachable_space;
            score += (double)(reachable - other_reachable) * weights->territory_delta;
        }

        if (weights->opponent_safe_moves != 0.0) {
            MoveDirection other_safe_moves[4];
            int other_safe_count = BoardSafeMoves(board, other->id, other_safe_moves);
            score -= (double)other_safe_count * weights->opponent_safe_moves;
        }

        if (weights->opponent_low_health_food_denial != 0.0 && other->health < weights->low_health_threshold) {
            for (int food_index = 0; food_index < board->food_count; food_index++) {
                Coord food = board->food[food_index];
                int my_food_distance = core_manhattan(head, food);
                int other_food_distance = core_manhattan(other_head, food);
                /*
                 * Score each food race separately so the evaluator can reward
                 * denying contested food while penalizing positions where a
                 * low-health opponent has cleaner access to another food item.
                 */
                if (my_food_distance <= other_food_distance) {
                    score += weights->opponent_low_health_food_denial / (double)(my_food_distance + 1);
                } else {
                    score -= weights->opponent_low_health_food_denial / (double)(other_food_distance + 1);
                }
            }
        }
    }

    *out_score = score;
    return CORE_OK;
}

CoreStatus CoreEvaluate(const Board* board, const char* snake_id, double* out_score) {
    CoreEvaluationWeights weights = CoreEvaluationWeightsDefault();
    return CoreEvaluateWithWeights(board, snake_id, &weights, out_score);
}
