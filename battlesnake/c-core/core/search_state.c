#include "search_state.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static char* duplicate_string(const char* value) {
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

static bool is_constrictor(const Board* board) {
    return board != NULL && board->ruleset_name != NULL && strcmp(board->ruleset_name, "constrictor") == 0;
}

static int coord_index_in_array(const Coord* coords, int count, Coord target) {
    for (int i = 0; i < count; i++) {
        if (CoordEquals(coords[i], target)) {
            return i;
        }
    }
    return -1;
}

static MoveDirection move_for_snake(const char* snake_id, const char** snake_ids, const MoveDirection* moves, int move_count) {
    for (int i = 0; i < move_count; i++) {
        if (strcmp(snake_id, snake_ids[i]) == 0) {
            return moves[i];
        }
    }
    return MOVE_INVALID;
}

static int snake_length(const Snake* snake) {
    return snake->length > 0 ? snake->length : snake->body_len;
}

static void clear_snake(Snake* snake) {
    SnakeFree(snake);
}

static void undo_snake_free(CoreUndoSnake* snake) {
    if (snake == NULL) {
        return;
    }
    free(snake->id);
    free(snake->name);
    free(snake->body);
    memset(snake, 0, sizeof(*snake));
}

static bool undo_snake_copy(CoreUndoSnake* target, const Snake* source) {
    memset(target, 0, sizeof(*target));
    target->id = duplicate_string(source->id);
    target->name = duplicate_string(source->name);
    target->health = source->health;
    target->body_len = source->body_len;
    target->length = source->length;
    if (source->body_len > 0) {
        target->body = (Coord*)malloc((size_t)source->body_len * sizeof(Coord));
        if (target->body == NULL) {
            undo_snake_free(target);
            return false;
        }
        memcpy(target->body, source->body, (size_t)source->body_len * sizeof(Coord));
    }
    if (target->id == NULL || target->name == NULL) {
        undo_snake_free(target);
        return false;
    }
    return true;
}

static void undo_frame_free(CoreUndoBoardFrame* frame) {
    if (frame == NULL) {
        return;
    }
    for (int i = 0; i < frame->snake_count; i++) {
        undo_snake_free(&frame->snakes[i]);
    }
    free(frame->snakes);
    free(frame->food);
    memset(frame, 0, sizeof(*frame));
}

static bool undo_frame_capture(CoreUndoBoardFrame* frame, const Board* board) {
    memset(frame, 0, sizeof(*frame));
    frame->snake_count = board->snake_count;
    frame->food_count = board->food_count;
    if (board->snake_count > 0) {
        frame->snakes = (CoreUndoSnake*)calloc((size_t)board->snake_count, sizeof(CoreUndoSnake));
        if (frame->snakes == NULL) {
            return false;
        }
        for (int i = 0; i < board->snake_count; i++) {
            if (!undo_snake_copy(&frame->snakes[i], &board->snakes[i])) {
                undo_frame_free(frame);
                return false;
            }
        }
    }
    if (board->food_count > 0) {
        frame->food = (Coord*)malloc((size_t)board->food_count * sizeof(Coord));
        if (frame->food == NULL) {
            undo_frame_free(frame);
            return false;
        }
        memcpy(frame->food, board->food, (size_t)board->food_count * sizeof(Coord));
    }
    return true;
}

static bool ensure_undo_capacity(CoreSearchState* state) {
    if (state->undo_count < state->undo_capacity) {
        return true;
    }
    int next_capacity = state->undo_capacity == 0 ? 32 : state->undo_capacity * 2;
    CoreUndoBoardFrame* next = (CoreUndoBoardFrame*)realloc(
        state->undo_stack,
        (size_t)next_capacity * sizeof(CoreUndoBoardFrame)
    );
    if (next == NULL) {
        return false;
    }
    memset(next + state->undo_capacity, 0, (size_t)(next_capacity - state->undo_capacity) * sizeof(CoreUndoBoardFrame));
    state->undo_stack = next;
    state->undo_capacity = next_capacity;
    return true;
}

static bool ensure_scratch_capacity(CoreSearchState* state, int snake_count) {
    if (snake_count <= state->scratch_capacity) {
        return true;
    }
    Coord* new_heads = (Coord*)malloc((size_t)snake_count * sizeof(Coord));
    bool* dead = (bool*)malloc((size_t)snake_count * sizeof(bool));
    bool* moved_flags = (bool*)malloc((size_t)snake_count * sizeof(bool));
    uint32_t* causes = (uint32_t*)malloc((size_t)snake_count * sizeof(uint32_t));
    if (new_heads == NULL || dead == NULL || moved_flags == NULL || causes == NULL) {
        free(new_heads);
        free(dead);
        free(moved_flags);
        free(causes);
        return false;
    }
    free(state->new_heads);
    free(state->dead);
    free(state->moved_flags);
    free(state->causes);
    state->new_heads = new_heads;
    state->dead = dead;
    state->moved_flags = moved_flags;
    state->causes = causes;
    state->scratch_capacity = snake_count;
    return true;
}

static bool copy_board_owned(Board* target, const Board* source) {
    Board* copy = BoardCopy(source);
    if (copy == NULL) {
        return false;
    }
    *target = *copy;
    free(copy);
    return true;
}

static bool ensure_snake_body_capacity(Snake* snake, int body_len) {
    if (body_len <= snake->body_len) {
        return true;
    }
    Coord* next = (Coord*)realloc(snake->body, (size_t)body_len * sizeof(Coord));
    if (next == NULL) {
        return false;
    }
    snake->body = next;
    return true;
}

static void apply_food_removal(Board* board, const bool* eaten_food) {
    int write = 0;
    for (int i = 0; i < board->food_count; i++) {
        if (!eaten_food[i]) {
            board->food[write++] = board->food[i];
        }
    }
    board->food_count = write;
}

static bool move_live_snake(Board* board, int index, MoveDirection move, bool* eaten_food) {
    Snake* snake = &board->snakes[index];
    Coord new_head = MoveStep(SnakeHead(snake), move);
    int food_index = coord_index_in_array(board->food, board->food_count, new_head);
    bool ate_food = food_index >= 0;
    if (ate_food && eaten_food != NULL) {
        eaten_food[food_index] = true;
    }
    bool grew = ate_food || is_constrictor(board);
    int new_body_len = snake->body_len + (grew ? 1 : 0);
    if (!ensure_snake_body_capacity(snake, new_body_len)) {
        return false;
    }
    int copy_count = grew ? snake->body_len : snake->body_len - 1;
    if (copy_count > 0) {
        memmove(&snake->body[1], snake->body, (size_t)copy_count * sizeof(Coord));
    }
    snake->body[0] = new_head;
    snake->body_len = new_body_len;
    snake->length = new_body_len;
    snake->health -= 1;
    if (coord_index_in_array(board->hazards, board->hazard_count, new_head) >= 0) {
        snake->health -= board->hazard_damage;
    }
    if (ate_food) {
        snake->health = 100;
    }
    return true;
}

static void resolve_body_collisions(Board* board, const Coord* new_heads, bool* dead, uint32_t* causes) {
    for (int i = 0; i < board->snake_count; i++) {
        if (dead[i]) {
            continue;
        }
        Coord head = new_heads[i];
        for (int j = 0; j < board->snake_count; j++) {
            const Snake* other = &board->snakes[j];
            for (int k = 1; k < other->body_len; k++) {
                if (CoordEquals(head, other->body[k])) {
                    dead[i] = true;
                    causes[i] |= i == j ? CORE_TERMINAL_CAUSE_SELF_BODY : CORE_TERMINAL_CAUSE_OTHER_BODY;
                }
            }
        }
    }
}

static void resolve_head_collisions(Board* board, const Coord* new_heads, bool* dead, uint32_t* causes) {
    for (int i = 0; i < board->snake_count; i++) {
        if (dead[i]) {
            continue;
        }
        for (int j = i + 1; j < board->snake_count; j++) {
            if (dead[j] || !CoordEquals(new_heads[i], new_heads[j])) {
                continue;
            }
            int left_len = snake_length(&board->snakes[i]);
            int right_len = snake_length(&board->snakes[j]);
            if (left_len > right_len) {
                dead[j] = true;
                causes[j] |= CORE_TERMINAL_CAUSE_HEAD_TO_HEAD;
            } else if (right_len > left_len) {
                dead[i] = true;
                causes[i] |= CORE_TERMINAL_CAUSE_HEAD_TO_HEAD;
            } else {
                dead[i] = true;
                dead[j] = true;
                causes[i] |= CORE_TERMINAL_CAUSE_HEAD_TO_HEAD;
                causes[j] |= CORE_TERMINAL_CAUSE_HEAD_TO_HEAD;
            }
        }
    }
}

static void compact_live_snakes(Board* board, const bool* dead) {
    int write = 0;
    for (int read = 0; read < board->snake_count; read++) {
        if (dead[read]) {
            clear_snake(&board->snakes[read]);
            continue;
        }
        if (write != read) {
            board->snakes[write] = board->snakes[read];
            memset(&board->snakes[read], 0, sizeof(Snake));
        }
        write++;
    }
    board->snake_count = write;
}

bool CoreSearchStateInit(CoreSearchState* state, const Board* board) {
    if (state == NULL || board == NULL) {
        return false;
    }
    memset(state, 0, sizeof(*state));
    if (!copy_board_owned(&state->board, board)) {
        return false;
    }
    state->food_capacity = state->board.food_count;
    return true;
}

void CoreSearchStateFree(CoreSearchState* state) {
    if (state == NULL) {
        return;
    }
    for (int i = 0; i < state->board.snake_count; i++) {
        clear_snake(&state->board.snakes[i]);
    }
    free(state->board.snakes);
    free(state->board.food);
    free(state->board.hazards);
    free(state->board.ruleset_name);
    for (int i = 0; i < state->undo_count; i++) {
        undo_frame_free(&state->undo_stack[i]);
    }
    free(state->undo_stack);
    free(state->new_heads);
    free(state->dead);
    free(state->moved_flags);
    free(state->causes);
    memset(state, 0, sizeof(*state));
}

bool CoreSearchStateMakeMovesDetailed(
    CoreSearchState* state,
    const char** snake_ids,
    const MoveDirection* moves,
    int move_count,
    uint32_t* out_causes,
    int causes_capacity
) {
    if (state == NULL || snake_ids == NULL || moves == NULL || move_count < 0) {
        return false;
    }
    Board* board = &state->board;
    if (!ensure_undo_capacity(state) || !ensure_scratch_capacity(state, board->snake_count)) {
        return false;
    }
    CoreUndoBoardFrame* frame = &state->undo_stack[state->undo_count];
    if (!undo_frame_capture(frame, board)) {
        return false;
    }

    memset(state->dead, 0, (size_t)board->snake_count * sizeof(bool));
    memset(state->moved_flags, 0, (size_t)board->snake_count * sizeof(bool));
    uint32_t* causes = state->causes;
    memset(causes, 0, (size_t)board->snake_count * sizeof(uint32_t));
    bool* eaten_food = NULL;
    if (board->food_count > 0) {
        eaten_food = (bool*)calloc((size_t)board->food_count, sizeof(bool));
        if (eaten_food == NULL) {
            undo_frame_free(frame);
            return false;
        }
    }

    for (int i = 0; i < board->snake_count; i++) {
        Snake* snake = &board->snakes[i];
        MoveDirection move = move_for_snake(snake->id, snake_ids, moves, move_count);
        if (move == MOVE_INVALID || snake->body_len == 0) {
            state->dead[i] = true;
            causes[i] |= CORE_TERMINAL_CAUSE_INVALID_COMMAND;
            state->new_heads[i] = SnakeHead(snake);
            continue;
        }
        state->new_heads[i] = MoveStep(SnakeHead(snake), move);
        state->moved_flags[i] = true;
        if (!move_live_snake(board, i, move, eaten_food)) {
            free(eaten_food);
            undo_frame_free(frame);
            return false;
        }
        if (!BoardInBounds(board, state->new_heads[i])) {
            state->dead[i] = true;
            causes[i] |= CORE_TERMINAL_CAUSE_WALL;
        }
        if (board->snakes[i].health <= 0) {
            state->dead[i] = true;
            bool in_hazard = coord_index_in_array(board->hazards, board->hazard_count, state->new_heads[i]) >= 0;
            causes[i] |= in_hazard ? CORE_TERMINAL_CAUSE_HAZARD : CORE_TERMINAL_CAUSE_STARVATION;
        }
    }

    resolve_body_collisions(board, state->new_heads, state->dead, causes);
    resolve_head_collisions(board, state->new_heads, state->dead, causes);
    if (out_causes != NULL && causes_capacity > 0) {
        int copy_count = board->snake_count < causes_capacity ? board->snake_count : causes_capacity;
        memcpy(out_causes, causes, (size_t)copy_count * sizeof(uint32_t));
    }
    compact_live_snakes(board, state->dead);
    if (eaten_food != NULL) {
        apply_food_removal(board, eaten_food);
    }
    free(eaten_food);
    state->undo_count++;
    return true;
}

bool CoreSearchStateMakeMoves(CoreSearchState* state, const char** snake_ids, const MoveDirection* moves, int move_count) {
    return CoreSearchStateMakeMovesDetailed(state, snake_ids, moves, move_count, NULL, 0);
}

bool CoreSearchStateUnmake(CoreSearchState* state) {
    if (state == NULL || state->undo_count <= 0) {
        return false;
    }
    Board* board = &state->board;
    CoreUndoBoardFrame* frame = &state->undo_stack[state->undo_count - 1];
    assert(frame->food_count <= state->food_capacity);
    if (frame->food_count > state->food_capacity) {
        return false;
    }

    for (int i = 0; i < board->snake_count; i++) {
        clear_snake(&board->snakes[i]);
    }

    for (int i = 0; i < frame->snake_count; i++) {
        board->snakes[i].id = frame->snakes[i].id;
        board->snakes[i].name = frame->snakes[i].name;
        board->snakes[i].health = frame->snakes[i].health;
        board->snakes[i].body = frame->snakes[i].body;
        board->snakes[i].body_len = frame->snakes[i].body_len;
        board->snakes[i].length = frame->snakes[i].length;
        memset(&frame->snakes[i], 0, sizeof(CoreUndoSnake));
    }
    board->snake_count = frame->snake_count;
    if (frame->food_count > 0) {
        memcpy(board->food, frame->food, (size_t)frame->food_count * sizeof(Coord));
    }
    board->food_count = frame->food_count;

    undo_frame_free(frame);
    state->undo_count--;
    return true;
}

const Board* CoreSearchStateBoard(const CoreSearchState* state) {
    return state == NULL ? NULL : &state->board;
}
