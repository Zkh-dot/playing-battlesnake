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
    double first_strategy[4];
    double second_strategy[4];
} PositionEvalValue;

typedef struct {
    PositionEvalTimer timer;
    CorePositionEvalConfig config;
    CorePositionEvalResult* result;
} PositionEvalContext;

static int board_command_moves(const Board* board, const char* snake_id, MoveDirection out_moves[4]) {
    const Snake* snake = BoardFindSnakeConst(board, snake_id);
    if (snake == NULL || snake->body_len == 0) {
        return 0;
    }

    out_moves[0] = MOVE_UP;
    out_moves[1] = MOVE_DOWN;
    out_moves[2] = MOVE_LEFT;
    out_moves[3] = MOVE_RIGHT;
    return 4;
}

static void clear_strategy(double strategy[4]) {
    for (int i = 0; i < 4; i++) {
        strategy[i] = 0.0;
    }
}

static PositionEvalValue position_eval_value(double probability, double confidence) {
    PositionEvalValue value;
    memset(&value, 0, sizeof(value));
    value.p = probability;
    value.confidence = confidence;
    return value;
}

static void copy_strategy_by_moves(
    const MoveDirection moves[4],
    const double local_strategy[4],
    int move_count,
    double out_strategy[4]
) {
    clear_strategy(out_strategy);
    for (int i = 0; i < move_count; i++) {
        if (moves[i] >= MOVE_UP && moves[i] <= MOVE_RIGHT) {
            out_strategy[moves[i]] = clamp01(local_strategy[i]);
        }
    }
}

static bool evaluate_no_command_terminal(
    int first_count,
    int second_count,
    PositionEvalContext* context,
    PositionEvalValue* out_value
) {
    if (first_count > 0 && second_count > 0) {
        return false;
    }

    context->result->terminal_leaves++;
    if (first_count <= 0 && second_count <= 0) {
        *out_value = position_eval_value(0.5, 1.0);
        return true;
    }
    if (first_count <= 0) {
        *out_value = position_eval_value(0.0, 1.0);
        return true;
    }
    *out_value = position_eval_value(1.0, 1.0);
    return true;
}

#ifdef CORE_POSITION_EVAL_TESTING
static bool position_eval_test_force_timeout = false;
static int position_eval_test_force_timeout_after_checks = -1;
#endif

static CoreStatus position_fill_timeout_matrix(
    int rows,
    int cols,
    bool evaluated[4][4],
    double probability_matrix[4][4],
    double confidence_matrix[4][4],
    double fallback_probability
);

static CoreStatus evaluate_node(
    const Board* board,
    const char* first_snake_id,
    const char* second_snake_id,
    int depth_remaining,
    PositionEvalContext* context,
    PositionEvalValue* out_value
);

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
#ifdef CORE_POSITION_EVAL_TESTING
    if (position_eval_test_force_timeout) {
        return true;
    }
    if (position_eval_test_force_timeout_after_checks >= 0) {
        if (position_eval_test_force_timeout_after_checks == 0) {
            return true;
        }
        position_eval_test_force_timeout_after_checks--;
        return false;
    }
#endif
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

static bool is_close(double a, double b) {
    return fabs(a - b) <= 1e-12;
}

static int popcount4(int mask) {
    int count = 0;
    for (int i = 0; i < 4; i++) {
        if ((mask & (1 << i)) != 0) {
            count++;
        }
    }
    return count;
}

static int mask_to_indices(int mask, int indices[4]) {
    int count = 0;
    for (int i = 0; i < 4; i++) {
        if ((mask & (1 << i)) != 0) {
            indices[count++] = i;
        }
    }
    return count;
}

static bool solve_linear_system(int n, double augmented[5][6], double out[5]) {
    for (int col = 0; col < n; col++) {
        int pivot = col;
        double pivot_abs = fabs(augmented[col][col]);
        for (int row = col + 1; row < n; row++) {
            double value_abs = fabs(augmented[row][col]);
            if (value_abs > pivot_abs) {
                pivot = row;
                pivot_abs = value_abs;
            }
        }
        if (pivot_abs < 1e-10) {
            return false;
        }
        if (pivot != col) {
            for (int k = col; k <= n; k++) {
                double tmp = augmented[col][k];
                augmented[col][k] = augmented[pivot][k];
                augmented[pivot][k] = tmp;
            }
        }

        double divisor = augmented[col][col];
        for (int k = col; k <= n; k++) {
            augmented[col][k] /= divisor;
        }
        for (int row = 0; row < n; row++) {
            if (row == col) {
                continue;
            }
            double factor = augmented[row][col];
            for (int k = col; k <= n; k++) {
                augmented[row][k] -= factor * augmented[col][k];
            }
        }
    }

    for (int i = 0; i < n; i++) {
        out[i] = augmented[i][n];
    }
    return true;
}

static bool solve_row_strategy_lp(
    double values[4][4],
    int rows,
    int cols,
    double row_strategy[4],
    double* value
) {
    bool found = false;
    double best_value = -DBL_MAX;
    double best_strategy[4] = {0.0};

    for (int row_mask = 1; row_mask < (1 << rows); row_mask++) {
        int support_size = popcount4(row_mask);
        if (support_size > cols) {
            continue;
        }
        for (int col_mask = 1; col_mask < (1 << cols); col_mask++) {
            if (popcount4(col_mask) != support_size) {
                continue;
            }

            int row_indices[4] = {0};
            int col_indices[4] = {0};
            mask_to_indices(row_mask, row_indices);
            mask_to_indices(col_mask, col_indices);

            int unknowns = support_size + 1;
            double augmented[5][6] = {{0.0}};
            double solution[5] = {0.0};
            for (int i = 0; i < support_size; i++) {
                augmented[0][i] = 1.0;
            }
            augmented[0][unknowns] = 1.0;
            for (int eq = 0; eq < support_size; eq++) {
                int col = col_indices[eq];
                for (int i = 0; i < support_size; i++) {
                    augmented[eq + 1][i] = values[row_indices[i]][col];
                }
                augmented[eq + 1][support_size] = -1.0;
            }

            if (!solve_linear_system(unknowns, augmented, solution)) {
                continue;
            }

            double candidate_strategy[4] = {0.0};
            bool valid = true;
            for (int i = 0; i < support_size; i++) {
                if (solution[i] < -1e-9) {
                    valid = false;
                    break;
                }
                candidate_strategy[row_indices[i]] = fmax(solution[i], 0.0);
            }
            if (!valid) {
                continue;
            }

            double candidate_value = solution[support_size];
            for (int col = 0; col < cols; col++) {
                double payoff = 0.0;
                for (int row = 0; row < rows; row++) {
                    payoff += candidate_strategy[row] * values[row][col];
                }
                if (payoff + 1e-9 < candidate_value) {
                    valid = false;
                    break;
                }
            }
            if (!valid) {
                continue;
            }

            if (!found || candidate_value > best_value) {
                found = true;
                best_value = candidate_value;
                memcpy(best_strategy, candidate_strategy, sizeof(best_strategy));
            }
        }
    }

    if (!found) {
        return false;
    }
    memcpy(row_strategy, best_strategy, sizeof(best_strategy));
    *value = best_value;
    return true;
}

static bool solve_col_strategy_lp(
    double values[4][4],
    int rows,
    int cols,
    double col_strategy[4],
    double* value
) {
    bool found = false;
    double best_value = DBL_MAX;
    double best_strategy[4] = {0.0};

    for (int col_mask = 1; col_mask < (1 << cols); col_mask++) {
        int support_size = popcount4(col_mask);
        if (support_size > rows) {
            continue;
        }
        for (int row_mask = 1; row_mask < (1 << rows); row_mask++) {
            if (popcount4(row_mask) != support_size) {
                continue;
            }

            int row_indices[4] = {0};
            int col_indices[4] = {0};
            mask_to_indices(row_mask, row_indices);
            mask_to_indices(col_mask, col_indices);

            int unknowns = support_size + 1;
            double augmented[5][6] = {{0.0}};
            double solution[5] = {0.0};
            for (int j = 0; j < support_size; j++) {
                augmented[0][j] = 1.0;
            }
            augmented[0][unknowns] = 1.0;
            for (int eq = 0; eq < support_size; eq++) {
                int row = row_indices[eq];
                for (int j = 0; j < support_size; j++) {
                    augmented[eq + 1][j] = values[row][col_indices[j]];
                }
                augmented[eq + 1][support_size] = -1.0;
            }

            if (!solve_linear_system(unknowns, augmented, solution)) {
                continue;
            }

            double candidate_strategy[4] = {0.0};
            bool valid = true;
            for (int j = 0; j < support_size; j++) {
                if (solution[j] < -1e-9) {
                    valid = false;
                    break;
                }
                candidate_strategy[col_indices[j]] = fmax(solution[j], 0.0);
            }
            if (!valid) {
                continue;
            }

            double candidate_value = solution[support_size];
            for (int row = 0; row < rows; row++) {
                double payoff = 0.0;
                for (int col = 0; col < cols; col++) {
                    payoff += candidate_strategy[col] * values[row][col];
                }
                if (payoff - 1e-9 > candidate_value) {
                    valid = false;
                    break;
                }
            }
            if (!valid) {
                continue;
            }

            if (!found || candidate_value < best_value) {
                found = true;
                best_value = candidate_value;
                memcpy(best_strategy, candidate_strategy, sizeof(best_strategy));
            }
        }
    }

    if (!found) {
        return false;
    }
    memcpy(col_strategy, best_strategy, sizeof(best_strategy));
    *value = best_value;
    return true;
}

static double pure_matrix_value_with_confidence(
    double values[4][4],
    double confidences[4][4],
    int rows,
    int cols,
    double row_strategy[4],
    double col_strategy[4],
    double* out_confidence
) {
    for (int i = 0; i < 4; i++) {
        row_strategy[i] = 0.0;
        col_strategy[i] = 0.0;
    }
    if (rows <= 0 || cols <= 0) {
        if (out_confidence != NULL) {
            *out_confidence = 0.0;
        }
        return 0.5;
    }

    double best_row_value = -DBL_MAX;
    double best_row_confidence = 0.0;
    int best_row = 0;
    int best_row_worst_col = 0;

    for (int i = 0; i < rows; i++) {
        double row_worst = DBL_MAX;
        double row_worst_confidence = 0.0;
        int row_worst_col = 0;
        for (int j = 0; j < cols; j++) {
            if (values[i][j] < row_worst) {
                row_worst = values[i][j];
                row_worst_confidence = confidences[i][j];
                row_worst_col = j;
                continue;
            }
            if (is_close(values[i][j], row_worst) && confidences[i][j] > row_worst_confidence) {
                row_worst_confidence = confidences[i][j];
                row_worst_col = j;
            }
        }

        if (row_worst > best_row_value) {
            best_row_value = row_worst;
            best_row_confidence = row_worst_confidence;
            best_row = i;
            best_row_worst_col = row_worst_col;
            continue;
        }
        if (is_close(row_worst, best_row_value) && row_worst_confidence > best_row_confidence) {
            best_row_confidence = row_worst_confidence;
            best_row = i;
            best_row_worst_col = row_worst_col;
        }
    }

    row_strategy[best_row] = 1.0;
    col_strategy[best_row_worst_col] = 1.0;
    if (out_confidence != NULL) {
        *out_confidence = best_row_confidence;
    }
    return best_row_value;
}

static bool matrix_all_values_close(double values[4][4], int rows, int cols) {
    if (rows <= 0 || cols <= 0) {
        return true;
    }
    double first = values[0][0];
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            if (!is_close(values[i][j], first)) {
                return false;
            }
        }
    }
    return true;
}

static double solve_zero_sum_matrix_with_confidence(
    double values[4][4],
    double confidences[4][4],
    int rows,
    int cols,
    double row_strategy[4],
    double col_strategy[4],
    double* out_confidence
) {
    if (matrix_all_values_close(values, rows, cols)) {
        return pure_matrix_value_with_confidence(
            values,
            confidences,
            rows,
            cols,
            row_strategy,
            col_strategy,
            out_confidence
        );
    }

    double row_value = 0.0;
    double col_value = 0.0;
    /*
     * The support solvers can reject singular degenerate matrices or supports
     * whose exact solution falls outside [0, 1]. In those cases the pure
     * maximin fallback below is the intended conservative behavior.
     */
    if (
        rows > 0 &&
        cols > 0 &&
        rows <= 4 &&
        cols <= 4 &&
        solve_row_strategy_lp(values, rows, cols, row_strategy, &row_value) &&
        solve_col_strategy_lp(values, rows, cols, col_strategy, &col_value) &&
        fabs(row_value - col_value) <= 1e-6
    ) {
        if (out_confidence != NULL) {
            double confidence = 0.0;
            for (int i = 0; i < rows; i++) {
                for (int j = 0; j < cols; j++) {
                    confidence += row_strategy[i] * col_strategy[j] * confidences[i][j];
                }
            }
            *out_confidence = confidence;
        }
        return row_value;
    }
    return pure_matrix_value_with_confidence(
        values,
        confidences,
        rows,
        cols,
        row_strategy,
        col_strategy,
        out_confidence
    );
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
    *out_value = position_eval_value(probability, 0.0);
    return CORE_OK;
}

static CoreStatus fill_timeout_matrix(
    const Board* board,
    const char* first_snake_id,
    const char* second_snake_id,
    int rows,
    int cols,
    bool evaluated[4][4],
    double probability_matrix[4][4],
    double confidence_matrix[4][4],
    PositionEvalContext* context
) {
    double fallback_probability = 0.5;
    CoreStatus status = heuristic_probability(
        board,
        first_snake_id,
        second_snake_id,
        &context->config.weights,
        &fallback_probability
    );
    if (status != CORE_OK) {
        return status;
    }

    status = position_fill_timeout_matrix(
        rows,
        cols,
        evaluated,
        probability_matrix,
        confidence_matrix,
        fallback_probability
    );
    if (status != CORE_OK) {
        return status;
    }

    context->result->timed_out = true;
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            if (!evaluated[i][j]) {
                context->result->timeout_leaves++;
                context->result->heuristic_leaves++;
            }
        }
    }
    return CORE_OK;
}

static CoreStatus position_fill_timeout_matrix(
    int rows,
    int cols,
    bool evaluated[4][4],
    double probability_matrix[4][4],
    double confidence_matrix[4][4],
    double fallback_probability
) {
    if (rows <= 0 || cols <= 0 || rows > 4 || cols > 4) {
        return CORE_ERROR;
    }
    if (fallback_probability < 0.0 || fallback_probability > 1.0) {
        return CORE_ERROR;
    }

    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            if (!evaluated[i][j]) {
                probability_matrix[i][j] = fallback_probability;
                confidence_matrix[i][j] = 0.0;
            }
        }
    }
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
        *out_value = position_eval_value(1.0, 1.0);
        return CORE_OK;
    }
    if (!first_alive && second_alive) {
        context->result->terminal_leaves++;
        *out_value = position_eval_value(0.0, 1.0);
        return CORE_OK;
    }
    if (!first_alive && !second_alive) {
        context->result->terminal_leaves++;
        *out_value = position_eval_value(0.5, 1.0);
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
    int first_count = board_command_moves(board, first_snake_id, first_moves);
    int second_count = board_command_moves(board, second_snake_id, second_moves);
    if (evaluate_no_command_terminal(first_count, second_count, context, out_value)) {
        return CORE_OK;
    }

    double probability_matrix[4][4] = {{0.0}};
    double confidence_matrix[4][4] = {{0.0}};
    bool evaluated[4][4] = {{false}};
    double row_strategy[4] = {0.0};
    double col_strategy[4] = {0.0};
    const char* ids[2] = {first_snake_id, second_snake_id};
    MoveDirection moves[2];
    bool timed_out = false;

    for (int i = 0; i < first_count; i++) {
        for (int j = 0; j < second_count; j++) {
            if (position_timed_out(&context->timer)) {
                CoreStatus status = fill_timeout_matrix(
                    board,
                    first_snake_id,
                    second_snake_id,
                    first_count,
                    second_count,
                    evaluated,
                    probability_matrix,
                    confidence_matrix,
                    context
                );
                if (status != CORE_OK) {
                    return status;
                }
                timed_out = true;
                break;
            }

            moves[0] = first_moves[i];
            moves[1] = second_moves[j];
            Board* child = BoardCloneAndApply(board, ids, moves, 2);
            if (child == NULL) {
                return CORE_ERROR;
            }

            PositionEvalValue child_value;
            CoreStatus status = evaluate_node(
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
            probability_matrix[i][j] = child_value.p;
            confidence_matrix[i][j] = child_value.confidence;
            evaluated[i][j] = true;
        }
        if (timed_out) {
            break;
        }
    }

    double value = pure_matrix_value_with_confidence(
        probability_matrix,
        confidence_matrix,
        first_count,
        second_count,
        row_strategy,
        col_strategy,
        NULL
    );
    double confidence = 0.0;
    for (int i = 0; i < first_count; i++) {
        for (int j = 0; j < second_count; j++) {
            confidence += row_strategy[i] * col_strategy[j] * confidence_matrix[i][j];
        }
    }

    *out_value = position_eval_value(clamp01(value), clamp01(confidence));
    copy_strategy_by_moves(first_moves, row_strategy, first_count, out_value->first_strategy);
    copy_strategy_by_moves(second_moves, col_strategy, second_count, out_value->second_strategy);
    return CORE_OK;
}

static CoreStatus evaluate_node_matrix(
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
        *out_value = position_eval_value(1.0, 1.0);
        return CORE_OK;
    }
    if (!first_alive && second_alive) {
        context->result->terminal_leaves++;
        *out_value = position_eval_value(0.0, 1.0);
        return CORE_OK;
    }
    if (!first_alive && !second_alive) {
        context->result->terminal_leaves++;
        *out_value = position_eval_value(0.5, 1.0);
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
    int first_count = board_command_moves(board, first_snake_id, first_moves);
    int second_count = board_command_moves(board, second_snake_id, second_moves);
    if (evaluate_no_command_terminal(first_count, second_count, context, out_value)) {
        return CORE_OK;
    }

    double probability_matrix[4][4] = {{0.0}};
    double confidence_matrix[4][4] = {{0.0}};
    bool evaluated[4][4] = {{false}};
    double row_strategy[4] = {0.0};
    double col_strategy[4] = {0.0};
    const char* ids[2] = {first_snake_id, second_snake_id};
    MoveDirection moves[2];
    bool timed_out = false;

    for (int i = 0; i < first_count; i++) {
        for (int j = 0; j < second_count; j++) {
            if (position_timed_out(&context->timer)) {
                CoreStatus status = fill_timeout_matrix(
                    board,
                    first_snake_id,
                    second_snake_id,
                    first_count,
                    second_count,
                    evaluated,
                    probability_matrix,
                    confidence_matrix,
                    context
                );
                if (status != CORE_OK) {
                    return status;
                }
                timed_out = true;
                break;
            }

            moves[0] = first_moves[i];
            moves[1] = second_moves[j];
            Board* child = BoardCloneAndApply(board, ids, moves, 2);
            if (child == NULL) {
                return CORE_ERROR;
            }

            PositionEvalValue child_value;
            CoreStatus status = evaluate_node(
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
            probability_matrix[i][j] = child_value.p;
            confidence_matrix[i][j] = child_value.confidence;
            evaluated[i][j] = true;
        }
        if (timed_out) {
            break;
        }
    }
    double confidence = 0.0;
    double value = solve_zero_sum_matrix_with_confidence(
        probability_matrix,
        confidence_matrix,
        first_count,
        second_count,
        row_strategy,
        col_strategy,
        &confidence
    );

    *out_value = position_eval_value(clamp01(value), clamp01(confidence));
    copy_strategy_by_moves(first_moves, row_strategy, first_count, out_value->first_strategy);
    copy_strategy_by_moves(second_moves, col_strategy, second_count, out_value->second_strategy);
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
    if (context->config.decision_mode == CORE_POSITION_DECISION_MATRIX) {
        return evaluate_node_matrix(
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

#ifdef CORE_POSITION_EVAL_TESTING
typedef struct {
    double probability;
    double confidence;
} CorePositionEvalTestTimeoutBackupResult;

typedef struct {
    double probability;
    double row_strategy[4];
    double col_strategy[4];
} CorePositionEvalTestStrategyResult;

void CorePositionEvalTestForceTimeout(bool enabled) {
    position_eval_test_force_timeout = enabled;
    position_eval_test_force_timeout_after_checks = -1;
}

void CorePositionEvalTestForceTimeoutAfterChecks(int checks) {
    position_eval_test_force_timeout = false;
    position_eval_test_force_timeout_after_checks = checks < 0 ? -1 : checks;
}

double CorePositionEvalTestSolveMatrix2x2(double a, double b, double c, double d) {
    double matrix[4][4] = {{0}};
    matrix[0][0] = a;
    matrix[0][1] = b;
    matrix[1][0] = c;
    matrix[1][1] = d;
    double confidence_matrix[4][4] = {{0}};
    double row_strategy[4] = {0};
    double col_strategy[4] = {0};
    double confidence = 0.0;
    return solve_zero_sum_matrix_with_confidence(
        matrix,
        confidence_matrix,
        2,
        2,
        row_strategy,
        col_strategy,
        &confidence
    );
}

double CorePositionEvalTestSolveMatrix2x2WithConfidence(
    double a,
    double b,
    double c,
    double d,
    double c_ab,
    double c_ac,
    double c_bc,
    double c_bd
) {
    double values[4][4] = {{0}};
    values[0][0] = a;
    values[0][1] = b;
    values[1][0] = c;
    values[1][1] = d;
    double confidence_matrix[4][4] = {{0}};
    confidence_matrix[0][0] = c_ab;
    confidence_matrix[0][1] = c_ac;
    confidence_matrix[1][0] = c_bc;
    confidence_matrix[1][1] = c_bd;
    double row_strategy[4] = {0};
    double col_strategy[4] = {0};
    double confidence = 0.0;
    (void)solve_zero_sum_matrix_with_confidence(
        values,
        confidence_matrix,
        2,
        2,
        row_strategy,
        col_strategy,
        &confidence
    );
    return confidence;
}

CorePositionEvalTestTimeoutBackupResult CorePositionEvalTestFillTimeoutMatrix2x2(
    double a,
    double b,
    double c,
    double d,
    double c_ab,
    double c_ac,
    double c_bc,
    double c_bd,
    bool e_ab,
    bool e_ac,
    bool e_bc,
    bool e_bd,
    double fallback_probability
) {
    bool evaluated[4][4] = {false};
    double probability_matrix[4][4] = {{0.0}};
    double confidence_matrix[4][4] = {{0.0}};

    probability_matrix[0][0] = a;
    probability_matrix[0][1] = b;
    probability_matrix[1][0] = c;
    probability_matrix[1][1] = d;
    confidence_matrix[0][0] = c_ab;
    confidence_matrix[0][1] = c_ac;
    confidence_matrix[1][0] = c_bc;
    confidence_matrix[1][1] = c_bd;
    evaluated[0][0] = e_ab;
    evaluated[0][1] = e_ac;
    evaluated[1][0] = e_bc;
    evaluated[1][1] = e_bd;

    CoreStatus status = position_fill_timeout_matrix(
        2,
        2,
        evaluated,
        probability_matrix,
        confidence_matrix,
        fallback_probability
    );
    if (status != CORE_OK) {
        return (CorePositionEvalTestTimeoutBackupResult){0.5, 0.0};
    }

    double row_strategy[4] = {0.0};
    double col_strategy[4] = {0.0};
    double confidence = 0.0;
    double probability = solve_zero_sum_matrix_with_confidence(
        probability_matrix,
        confidence_matrix,
        2,
        2,
        row_strategy,
        col_strategy,
        &confidence
    );
    return (CorePositionEvalTestTimeoutBackupResult){probability, confidence};
}

CorePositionEvalTestStrategyResult CorePositionEvalTestSolveMatrix4x4Strategies(
    const double values_in[16]
) {
    double values[4][4] = {{0.0}};
    double confidences[4][4] = {{0.0}};
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            values[i][j] = values_in[i * 4 + j];
        }
    }

    CorePositionEvalTestStrategyResult result;
    memset(&result, 0, sizeof(result));
    result.probability = solve_zero_sum_matrix_with_confidence(
        values,
        confidences,
        4,
        4,
        result.row_strategy,
        result.col_strategy,
        NULL
    );
    return result;
}
#endif

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
    memset(out_result, 0, sizeof(*out_result));
    if (config.max_depth < 0) {
        return CORE_ERROR;
    }
    if (
        config.decision_mode != CORE_POSITION_DECISION_MATRIX &&
        config.decision_mode != CORE_POSITION_DECISION_PURE_MINIMAX
    ) {
        return CORE_ERROR;
    }

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
        memset(out_result, 0, sizeof(*out_result));
        return status;
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    out_result->first_win_probability = clamp01(value.p);
    out_result->confidence = clamp01(value.confidence);
    for (int i = 0; i < 4; i++) {
        out_result->first_move_probabilities[i] = clamp01(value.first_strategy[i]);
        out_result->second_move_probabilities[i] = clamp01(value.second_strategy[i]);
    }
    out_result->elapsed_ms = position_elapsed_ms(start, end);
    return CORE_OK;
}
