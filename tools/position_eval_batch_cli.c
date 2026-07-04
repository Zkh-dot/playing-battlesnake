#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "../battlesnake/c-core/core/position_eval.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static MoveDirection parse_move(const char* value) {
    if (strcmp(value, "none") == 0) {
        return MOVE_INVALID;
    }
    return MoveDirectionFromString(value);
}

static int read_token(char* buffer, size_t size) {
    return scanf("%255s", buffer) == 1 && size > 0;
}

static Board* read_board(void) {
    int width = 0;
    int height = 0;
    int hazard_damage = 0;
    char ruleset[256];
    if (scanf("%d %d %d", &width, &height, &hazard_damage) != 3) {
        return NULL;
    }
    if (!read_token(ruleset, sizeof(ruleset))) {
        return NULL;
    }

    Board* board = BoardCreate(width, height, ruleset, hazard_damage);
    if (board == NULL) {
        return NULL;
    }

    int snake_count = 0;
    if (scanf("%d", &snake_count) != 1 || snake_count < 0) {
        BoardFree(board);
        return NULL;
    }

    for (int i = 0; i < snake_count; i++) {
        char snake_id[256];
        int health = 0;
        int body_len = 0;
        if (!read_token(snake_id, sizeof(snake_id)) || scanf("%d %d", &health, &body_len) != 2) {
            BoardFree(board);
            return NULL;
        }
        if (body_len < 0) {
            BoardFree(board);
            return NULL;
        }

        Coord* body = NULL;
        if (body_len > 0) {
            body = (Coord*)malloc((size_t)body_len * sizeof(Coord));
            if (body == NULL) {
                BoardFree(board);
                return NULL;
            }
        }
        for (int j = 0; j < body_len; j++) {
            if (scanf("%d %d", &body[j].x, &body[j].y) != 2) {
                free(body);
                BoardFree(board);
                return NULL;
            }
        }

        Snake snake;
        SnakeInit(&snake, snake_id, snake_id, health, body, body_len);
        free(body);
        if (!BoardAddSnake(board, &snake)) {
            SnakeFree(&snake);
            BoardFree(board);
            return NULL;
        }
        SnakeFree(&snake);
    }

    int food_count = 0;
    if (scanf("%d", &food_count) != 1 || food_count < 0) {
        BoardFree(board);
        return NULL;
    }
    for (int i = 0; i < food_count; i++) {
        Coord food;
        if (scanf("%d %d", &food.x, &food.y) != 2 || !BoardAddFood(board, food)) {
            BoardFree(board);
            return NULL;
        }
    }

    int hazard_count = 0;
    if (scanf("%d", &hazard_count) != 1 || hazard_count < 0) {
        BoardFree(board);
        return NULL;
    }
    for (int i = 0; i < hazard_count; i++) {
        Coord hazard;
        if (scanf("%d %d", &hazard.x, &hazard.y) != 2 || !BoardAddHazard(board, hazard)) {
            BoardFree(board);
            return NULL;
        }
    }

    return board;
}

static CoreStatus evaluate_case(
    Board* board,
    const char* first_id,
    const char* second_id,
    int budget_ms,
    int max_depth,
    CorePositionDecisionMode decision_mode,
    MoveDirection first_apply,
    MoveDirection second_apply,
    CorePositionEvalResult* result
) {
    Board* eval_board = board;
    Board* child = NULL;
    if (first_apply != MOVE_INVALID || second_apply != MOVE_INVALID) {
        const char* ids[2] = {first_id, second_id};
        MoveDirection moves[2] = {first_apply, second_apply};
        child = BoardCloneAndApply(board, ids, moves, 2);
        if (child == NULL) {
            return CORE_ERROR;
        }
        eval_board = child;
    }

    CorePositionEvalConfig config = CorePositionEvalConfigDefault(budget_ms);
    config.max_depth = max_depth;
    config.decision_mode = decision_mode;
    CoreStatus status = CorePositionEvaluateDuel(eval_board, first_id, second_id, config, result);
    BoardFree(child);
    return status;
}

int main(void) {
    int case_count = 0;
    if (scanf("%d", &case_count) != 1 || case_count < 0) {
        fprintf(stderr, "failed to read case count\n");
        return 2;
    }

    puts("case_id\tstatus\tp\tconfidence\tfirst_up\tfirst_down\tfirst_left\tfirst_right\tsecond_up\tsecond_down\tsecond_left\tsecond_right\tnodes\tterminal_leaves\theuristic_leaves\ttimeout_leaves\texpanded_children\tcompleted_depth\tmax_depth_started\ttimed_out\telapsed_ms");
    for (int i = 0; i < case_count; i++) {
        char case_id[256];
        char first_id[256];
        char second_id[256];
        char first_apply_text[256];
        char second_apply_text[256];
        int budget_ms = 0;
        int max_depth = 0;
        int decision_mode = 0;

        if (!read_token(case_id, sizeof(case_id)) ||
            !read_token(first_id, sizeof(first_id)) ||
            !read_token(second_id, sizeof(second_id)) ||
            scanf("%d %d %d", &budget_ms, &max_depth, &decision_mode) != 3 ||
            !read_token(first_apply_text, sizeof(first_apply_text)) ||
            !read_token(second_apply_text, sizeof(second_apply_text))) {
            fprintf(stderr, "failed to read case header %d\n", i);
            return 2;
        }

        Board* board = read_board();
        CorePositionEvalResult result;
        memset(&result, 0, sizeof(result));
        CoreStatus status = CORE_ERROR;
        if (board != NULL) {
            status = evaluate_case(
                board,
                first_id,
                second_id,
                budget_ms,
                max_depth,
                (CorePositionDecisionMode)decision_mode,
                parse_move(first_apply_text),
                parse_move(second_apply_text),
                &result
            );
        }

        printf(
            "%s\t%d\t%.17g\t%.17g\t%.17g\t%.17g\t%.17g\t%.17g\t%.17g\t%.17g\t%.17g\t%.17g\t%llu\t%llu\t%llu\t%llu\t%llu\t%d\t%d\t%d\t%.17g\n",
            case_id,
            (int)status,
            result.first_win_probability,
            result.confidence,
            result.first_move_probabilities[MOVE_UP],
            result.first_move_probabilities[MOVE_DOWN],
            result.first_move_probabilities[MOVE_LEFT],
            result.first_move_probabilities[MOVE_RIGHT],
            result.second_move_probabilities[MOVE_UP],
            result.second_move_probabilities[MOVE_DOWN],
            result.second_move_probabilities[MOVE_LEFT],
            result.second_move_probabilities[MOVE_RIGHT],
            (unsigned long long)result.nodes,
            (unsigned long long)result.terminal_leaves,
            (unsigned long long)result.heuristic_leaves,
            (unsigned long long)result.timeout_leaves,
            (unsigned long long)result.expanded_children,
            result.completed_depth,
            result.max_depth_started,
            result.timed_out ? 1 : 0,
            result.elapsed_ms
        );
        BoardFree(board);
    }

    return 0;
}
