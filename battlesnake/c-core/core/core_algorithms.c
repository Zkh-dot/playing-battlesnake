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

typedef struct {
    struct timespec deadline;
} CoreSearchTimer;

typedef struct {
    CoreSearchTimer timer;
    CoreSearchConfig config;
    CoreSearchStats* stats;
    CoreTranspositionTable tt;
    CoreSearchWorkspace workspace;
    CoreSearchState* state;
    bool tt_enabled;
    MoveDirection principal_variation[CORE_MINIMAX_MAX_DEPTH + 1];
    MoveDirection killer_moves[CORE_MINIMAX_MAX_DEPTH + 1][2];
    int history_scores[4];
} CoreSearchContext;

static CoreSearchTimer core_search_timer_start(int time_budget_ms) {
    if (time_budget_ms < 1) {
        time_budget_ms = 1;
    }

    struct timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec += time_budget_ms / 1000;
    deadline.tv_nsec += (long)(time_budget_ms % 1000) * 1000000L;
    while (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000L;
    }

    return (CoreSearchTimer){deadline};
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

static bool core_minimax_is_terminal(const Board* board, const char* snake_id) {
    const Snake* snake = BoardFindSnakeConst(board, snake_id);
    return snake == NULL || snake->body_len == 0 || board->snake_count <= 1;
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

static bool core_equal_score_move_is_better(
    const Board* board,
    const char* snake_id,
    const MoveDirection* original_moves,
    int move_count,
    int* reachable_spaces,
    MoveDirection candidate,
    MoveDirection current_best,
    MoveDirection preferred
) {
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

static CoreStatus core_minimax_search(
    const Board* board,
    const char* snake_id,
    int depth,
    int ply,
    double alpha,
    double beta,
    MoveDirection preferred_move,
    CoreSearchContext* context,
    bool* timed_out,
    double* out_score,
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

    bool tt_node_enabled = context->tt_enabled && depth >= 2;
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
        double tt_score = 0.0;
        bool tt_collision = false;
        CoreTtProbeResult tt_probe = CoreTtProbe(
            &context->tt,
            hash,
            depth,
            alpha,
            beta,
            &tt_score,
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
            *out_score = tt_score;
            if (out_best_move != NULL) {
                *out_best_move = tt_best_move;
            }
            return CORE_OK;
        }
    }

    if (depth <= 0 || core_minimax_is_terminal(board, snake_id)) {
        if (stats != NULL) {
            stats->leaf_evals++;
        }
        CoreStatus status = CoreEvaluate(board, snake_id, out_score);
        if (status == CORE_OK && tt_node_enabled) {
            bool tt_collision = false;
            bool stored = CoreTtStore(&context->tt, hash, depth, *out_score, CORE_TT_EXACT, MOVE_INVALID, &tt_collision);
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
            own_move_count = core_safe_moves_or_all(board, snake_id, own_moves);
        } else {
            if (stats != NULL) {
                stats->safe_move_calls++;
            }
            option_counts[i] = BoardSafeMoves(board, board->snakes[i].id, options[i]);
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
        CoreStatus status = CoreEvaluate(board, snake_id, out_score);
        if (status == CORE_OK && tt_node_enabled) {
            bool tt_collision = false;
            bool stored = CoreTtStore(&context->tt, hash, depth, *out_score, CORE_TT_EXACT, MOVE_INVALID, &tt_collision);
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
    double best_score = -DBL_MAX;
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

        double worst_reply = DBL_MAX;
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

            double score = 0.0;
            double child_beta = worst_reply < beta ? worst_reply : beta;
            if (context->config.enable_make_unmake && context->state != NULL) {
                const Board* current = CoreSearchStateBoard(context->state);
                if (current == NULL || current->snake_count != snake_count) {
                    return CORE_ERROR;
                }
                for (int i = 0; i < snake_count; i++) {
                    ids[i] = current->snakes[i].id;
                }
                if (!CoreSearchStateMakeMoves(context->state, ids, moves, snake_count)) {
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
                    context,
                    timed_out,
                    &score,
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
                Board* next = BoardCloneAndApply(board, ids, moves, snake_count);
                if (next == NULL) {
                    return CORE_ERROR;
                }
                CoreStatus status = core_minimax_search(
                    next,
                    snake_id,
                    depth - 1,
                    ply + 1,
                    alpha,
                    child_beta,
                    MOVE_INVALID,
                    context,
                    timed_out,
                    &score,
                    NULL
                );
                BoardFree(next);
                if (status != CORE_OK || *timed_out) {
                    return status;
                }
            }

            if (score < worst_reply) {
                worst_reply = score;
            }
            if (worst_reply <= alpha) {
                if (stats != NULL) {
                    stats->beta_cutoffs++;
                    if (order == 0) {
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

        if (
            worst_reply > best_score ||
            (
                worst_reply == best_score &&
                core_equal_score_move_is_better(
                    board,
                    snake_id,
                    original_own_moves,
                    own_move_count,
                    original_reachable_spaces,
                    own_move,
                    best_move,
                    tie_preferred
                )
            )
        ) {
            best_score = worst_reply;
            best_move = own_move;
        }
        if (best_score > alpha) {
            alpha = best_score;
        }
        if (alpha >= beta) {
            break;
        }
    }

    if (*timed_out) {
        return CORE_OK;
    }

    *out_score = best_score;
    if (out_best_move != NULL) {
        *out_best_move = best_move;
    }
    if (context->config.enable_move_ordering && ply >= 0 && ply <= CORE_MINIMAX_MAX_DEPTH) {
        context->principal_variation[ply] = best_move;
    }
    if (tt_node_enabled) {
        CoreTtBound bound = CORE_TT_EXACT;
        if (best_score <= original_alpha) {
            bound = CORE_TT_UPPER;
        } else if (best_score >= original_beta) {
            bound = CORE_TT_LOWER;
        }
        bool tt_collision = false;
        bool stored = CoreTtStore(&context->tt, hash, depth, best_score, bound, best_move, &tt_collision);
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

    MoveDirection safe_moves[4];
    stats->safe_move_calls++;
    core_safe_moves_or_all(board, snake_id, safe_moves);
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
    double completed_score = 0.0;
    bool timed_out = false;
    struct timespec start;
    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int depth = 1; depth <= max_depth; depth++) {
        bool iteration_timed_out = false;
        double score = 0.0;
        MoveDirection candidate = completed_best;
        stats->max_depth_started = depth;
        CoreStatus status = core_minimax_search(
            board,
            snake_id,
            depth,
            0,
            -DBL_MAX,
            DBL_MAX,
            preferred_move,
            &context,
            &iteration_timed_out,
            &score,
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
            timed_out = true;
            break;
        }

        completed_best = candidate;
        preferred_move = candidate;
        if (context.config.enable_move_ordering) {
            context.principal_variation[0] = candidate;
        }
        completed_depth = depth;
        completed_score = score;
        if (!fixed_depth_requested && (score >= 999999.0 || score <= -999999.0 || core_search_timed_out(&context.timer))) {
            break;
        }
        if (context.tt_enabled) {
            CoreTtNextGeneration(&context.tt);
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    stats->completed_depth = completed_depth;
    stats->timed_out = timed_out;
    stats->score = completed_score;
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

CoreStatus CoreEvaluate(const Board* board, const char* snake_id, double* out_score) {
    if (board == NULL || snake_id == NULL || out_score == NULL) {
        return CORE_ERROR;
    }

    const Snake* snake = BoardFindSnakeConst(board, snake_id);
    if (snake == NULL || snake->body_len == 0) {
        *out_score = -1000000.0;
        return CORE_OK;
    }

    if (board->snake_count == 1) {
        *out_score = 1000000.0;
        return CORE_OK;
    }

    Coord head = SnakeHead(snake);
    int reachable = 0;
    CoreStatus status = CoreReachableSpace(board, head, snake_id, &reachable);
    if (status != CORE_OK) {
        return status;
    }

    MoveDirection safe_moves[4];
    int safe_count = BoardSafeMoves(board, snake_id, safe_moves);
    int length = core_snake_length(snake);
    double score = 0.0;

    score += 500.0;
    score += (double)snake->health * 0.7;
    score += (double)length * 18.0;
    score += (double)reachable * 4.0;
    score += (double)safe_count * 35.0;
    score += core_center_score(board, head) * 2.0;

    int food_distance = core_nearest_food_distance(board, head);
    if (food_distance != INT_MAX) {
        double food_weight = snake->health < 35 ? 120.0 : 55.0;
        score += food_weight / (double)(food_distance + 1);
    }

    if (core_coord_in_array(board->hazards, board->hazard_count, head)) {
        score -= (double)(board->hazard_damage + 25);
    }

    for (int i = 0; i < board->snake_count; i++) {
        const Snake* other = &board->snakes[i];
        if (strcmp(other->id, snake_id) == 0 || other->body_len == 0) {
            continue;
        }

        Coord other_head = SnakeHead(other);
        int other_length = core_snake_length(other);
        int distance = core_manhattan(head, other_head);
        score += (double)(length - other_length) * 5.0;
        if (distance == 1 && other_length >= length) {
            score -= 120.0;
        } else if (distance == 1 && other_length < length) {
            score += 45.0;
        }
    }

    *out_score = score;
    return CORE_OK;
}
