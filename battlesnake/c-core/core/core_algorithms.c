#include "core_algorithms.h"

#include <float.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

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
    blocked[start_index] = 0;
    if (!blocked[start_index]) {
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

CoreStatus CoreMinimaxMove(
    const Board* board,
    const char* snake_id,
    int time_budget_ms,
    MoveDirection* out_move
) {
    (void)time_budget_ms;
    if (board == NULL || snake_id == NULL || out_move == NULL) {
        return CORE_ERROR;
    }

    const Snake* snake = BoardFindSnakeConst(board, snake_id);
    if (snake == NULL || snake->body_len == 0) {
        return CORE_ERROR;
    }

    MoveDirection safe_moves[4];
    int safe_count = core_safe_moves_or_all(board, snake_id, safe_moves);
    double best_score = -DBL_MAX;
    MoveDirection best_move = safe_moves[0];
    for (int move_index = 0; move_index < safe_count; move_index++) {
        const char** ids = (const char**)calloc((size_t)board->snake_count, sizeof(char*));
        MoveDirection* moves = (MoveDirection*)calloc((size_t)board->snake_count, sizeof(MoveDirection));
        int* option_counts = (int*)calloc((size_t)board->snake_count, sizeof(int));
        MoveDirection(*options)[4] = (MoveDirection(*)[4])calloc((size_t)board->snake_count, sizeof(MoveDirection[4]));
        if (ids == NULL || moves == NULL || option_counts == NULL || options == NULL) {
            free(ids);
            free(moves);
            free(option_counts);
            free(options);
            return CORE_ERROR;
        }

        int own_index = -1;
        for (int i = 0; i < board->snake_count; i++) {
            ids[i] = board->snakes[i].id;
            if (strcmp(board->snakes[i].id, snake_id) == 0) {
                own_index = i;
                option_counts[i] = 1;
                options[i][0] = safe_moves[move_index];
            } else {
                option_counts[i] = core_safe_moves_or_all(board, board->snakes[i].id, options[i]);
            }
        }

        if (own_index < 0) {
            free(ids);
            free(moves);
            free(option_counts);
            free(options);
            return CORE_ERROR;
        }

        int total = 1;
        for (int i = 0; i < board->snake_count; i++) {
            total *= option_counts[i];
        }

        double worst_reply = DBL_MAX;
        for (int combo = 0; combo < total; combo++) {
            int remainder = combo;
            for (int i = 0; i < board->snake_count; i++) {
                int option_index = remainder % option_counts[i];
                remainder /= option_counts[i];
                moves[i] = options[i][option_index];
            }

            Board* next = BoardCloneAndApply(board, ids, moves, board->snake_count);
            if (next == NULL) {
                free(ids);
                free(moves);
                free(option_counts);
                free(options);
                return CORE_ERROR;
            }

            double score = 0.0;
            if (BoardFindSnakeConst(next, snake_id) == NULL) {
                score = -1000000.0;
            } else {
                CoreStatus status = CoreEvaluate(next, snake_id, &score);
                if (status != CORE_OK) {
                    BoardFree(next);
                    free(ids);
                    free(moves);
                    free(option_counts);
                    free(options);
                    return status;
                }
            }
            BoardFree(next);

            if (score < worst_reply) {
                worst_reply = score;
            }
        }

        free(ids);
        free(moves);
        free(option_counts);
        free(options);

        if (worst_reply > best_score) {
            best_score = worst_reply;
            best_move = safe_moves[move_index];
        }
    }

    *out_move = best_move;
    return CORE_OK;
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

    if (child_count[start_index] > 1) {
        articulation[start_index] = 1;
    }
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
