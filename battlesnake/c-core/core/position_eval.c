#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "position_eval.h"

#include <float.h>
#include <math.h>
#include <time.h>
#include <string.h>

static double clamp01(double value) {
    if (!isfinite(value)) {
        return 0.5;
    }
    if (value < 0.0) {
        return 0.0;
    }
    if (value > 1.0) {
        return 1.0;
    }
    return value;
}

static double stable_sigmoid(double scaled) {
    if (scaled >= 0.0) {
        return 1.0 / (1.0 + exp(-scaled));
    }

    double z = exp(scaled);
    return z / (1.0 + z);
}

static CoreStatus heuristic_probability(
    const Board* board,
    const char* first_snake_id,
    const char* second_snake_id,
    const CoreEvaluationWeights* weights,
    double* out_probability
) {
    double first_score = 0.0;
    double second_score = 0.0;
    CoreStatus first_status = CoreEvaluateWithWeights(board, first_snake_id, weights, &first_score);
    CoreStatus second_status = CoreEvaluateWithWeights(board, second_snake_id, weights, &second_score);
    if (first_status != CORE_OK) {
        return first_status;
    }
    if (second_status != CORE_OK) {
        return second_status;
    }

    double diff = first_score - second_score;
    double probability = stable_sigmoid(diff / 250.0);
    *out_probability = clamp01(probability);
    return CORE_OK;
}

typedef struct {
    struct timespec deadline;
} PositionEvalTimer;

typedef struct {
    double p;
    double confidence;
} PositionEvalValue;

typedef struct {
    PositionEvalTimer timer;
    CorePositionEvalConfig config;
    CorePositionEvalResult* result;
} PositionEvalContext;

static PositionEvalTimer position_timer_start(int time_budget_ms) {
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
    return (PositionEvalTimer){deadline};
}

static bool position_timed_out(const PositionEvalTimer* timer) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec > timer->deadline.tv_sec ||
        (now.tv_sec == timer->deadline.tv_sec && now.tv_nsec >= timer->deadline.tv_nsec);
}

static double position_elapsed_ms(struct timespec start, struct timespec end) {
    long seconds = end.tv_sec - start.tv_sec;
    long nanoseconds = end.tv_nsec - start.tv_nsec;
    return (double)seconds * 1000.0 + (double)nanoseconds / 1000000.0;
}

static CoreStatus evaluate_heuristic_leaf(
    const Board* board,
    const char* first_snake_id,
    const char* second_snake_id,
    PositionEvalContext* context,
    bool timed_out,
    PositionEvalValue* out_value
) {
    double probability = 0.5;
    CoreStatus status = heuristic_probability(
        board,
        first_snake_id,
        second_snake_id,
        &context->config.weights,
        &probability
    );
    if (status != CORE_OK) {
        return status;
    }
    if (timed_out) {
        context->result->timed_out = true;
        context->result->timeout_leaves++;
    }
    context->result->heuristic_leaves++;
    *out_value = (PositionEvalValue){probability, 0.0};
    return CORE_OK;
}

static CoreStatus evaluate_node_pure_minimax(
    const Board* board,
    const char* first_snake_id,
    const char* second_snake_id,
    int depth_remaining,
    PositionEvalContext* context,
    PositionEvalValue* out_value
) {
    context->result->nodes++;

    const Snake* first = BoardFindSnakeConst(board, first_snake_id);
    const Snake* second = BoardFindSnakeConst(board, second_snake_id);
    bool first_alive = first != NULL && first->body_len > 0;
    bool second_alive = second != NULL && second->body_len > 0;

    if (first_alive && !second_alive) {
        context->result->terminal_leaves++;
        *out_value = (PositionEvalValue){1.0, 1.0};
        return CORE_OK;
    }
    if (!first_alive && second_alive) {
        context->result->terminal_leaves++;
        *out_value = (PositionEvalValue){0.0, 1.0};
        return CORE_OK;
    }
    if (!first_alive && !second_alive) {
        context->result->terminal_leaves++;
        *out_value = (PositionEvalValue){0.5, 1.0};
        return CORE_OK;
    }

    if (depth_remaining <= 0) {
        return evaluate_heuristic_leaf(
            board,
            first_snake_id,
            second_snake_id,
            context,
            false,
            out_value
        );
    }
    if (position_timed_out(&context->timer)) {
        return evaluate_heuristic_leaf(
            board,
            first_snake_id,
            second_snake_id,
            context,
            true,
            out_value
        );
    }

    MoveDirection first_moves[4];
    MoveDirection second_moves[4];
    int first_count = BoardSafeMoves(board, first_snake_id, first_moves);
    int second_count = BoardSafeMoves(board, second_snake_id, second_moves);
    if (first_count <= 0) {
        first_count = 1;
        first_moves[0] = MOVE_INVALID;
    }
    if (second_count <= 0) {
        second_count = 1;
        second_moves[0] = MOVE_INVALID;
    }

    double best_p = -DBL_MAX;
    double best_confidence = 0.0;
    const char* ids[2] = {first_snake_id, second_snake_id};
    MoveDirection moves[2];

    for (int i = 0; i < first_count; i++) {
        double worst_p = DBL_MAX;
        double worst_confidence = 0.0;
        for (int j = 0; j < second_count; j++) {
            if (position_timed_out(&context->timer)) {
                return evaluate_heuristic_leaf(
                    board,
                    first_snake_id,
                    second_snake_id,
                    context,
                    true,
                    out_value
                );
            }

            moves[0] = first_moves[i];
            moves[1] = second_moves[j];
            Board* child = BoardCloneAndApply(board, ids, moves, 2);
            if (child == NULL) {
                return CORE_ERROR;
            }

            PositionEvalValue child_value;
            CoreStatus status = evaluate_node_pure_minimax(
                child,
                first_snake_id,
                second_snake_id,
                depth_remaining - 1,
                context,
                &child_value
            );
            BoardFree(child);
            if (status != CORE_OK) {
                return status;
            }

            context->result->expanded_children++;
            if (child_value.p < worst_p) {
                worst_p = child_value.p;
                worst_confidence = child_value.confidence;
            }
        }
        if (worst_p > best_p) {
            best_p = worst_p;
            best_confidence = worst_confidence;
        }
    }

    *out_value = (PositionEvalValue){clamp01(best_p), clamp01(best_confidence)};
    return CORE_OK;
}

static CoreStatus evaluate_node(
    const Board* board,
    const char* first_snake_id,
    const char* second_snake_id,
    int depth_remaining,
    PositionEvalContext* context,
    PositionEvalValue* out_value
) {
    if (context->config.decision_mode == CORE_POSITION_DECISION_MATRIX) {
        // Matrix mode currently uses pure maximin backup until Task 5 adds a true matrix solver.
        return evaluate_node_pure_minimax(
            board,
            first_snake_id,
            second_snake_id,
            depth_remaining,
            context,
            out_value
        );
    }
    if (context->config.decision_mode == CORE_POSITION_DECISION_PURE_MINIMAX) {
        return evaluate_node_pure_minimax(
            board,
            first_snake_id,
            second_snake_id,
            depth_remaining,
            context,
            out_value
        );
    }
    return CORE_ERROR;
}

CorePositionEvalConfig CorePositionEvalConfigDefault(int time_budget_ms) {
    CorePositionEvalConfig config = {0};
    config.time_budget_ms = time_budget_ms < 1 ? 1 : time_budget_ms;
    config.weights = CoreEvaluationWeightsDefault();
    config.decision_mode = CORE_POSITION_DECISION_MATRIX;
    return config;
}

CoreStatus CorePositionEvaluateDuel(
    const Board* board,
    const char* first_snake_id,
    const char* second_snake_id,
    CorePositionEvalConfig config,
    CorePositionEvalResult* out_result
) {
    if (board == NULL || first_snake_id == NULL || second_snake_id == NULL || out_result == NULL) {
        return CORE_ERROR;
    }
    if (config.max_depth < 0) {
        return CORE_ERROR;
    }
    if (
        config.decision_mode != CORE_POSITION_DECISION_MATRIX &&
        config.decision_mode != CORE_POSITION_DECISION_PURE_MINIMAX
    ) {
        return CORE_ERROR;
    }

    memset(out_result, 0, sizeof(*out_result));
    struct timespec start;
    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    PositionEvalContext context;
    memset(&context, 0, sizeof(context));
    context.timer = position_timer_start(config.time_budget_ms);
    context.config = config;
    context.result = out_result;

    PositionEvalValue value;
    CoreStatus status = evaluate_node(
        board,
        first_snake_id,
        second_snake_id,
        config.max_depth,
        &context,
        &value
    );
    if (status != CORE_OK) {
        return status;
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    out_result->first_win_probability = clamp01(value.p);
    out_result->confidence = clamp01(value.confidence);
    out_result->elapsed_ms = position_elapsed_ms(start, end);
    return CORE_OK;
}
