#include "../../battlesnake/c-core/core/position_eval.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static Snake make_snake(const char* id, Coord* body, int body_len, int health) {
    Snake snake;
    SnakeInit(&snake, id, id, health, body, body_len);
    snake.length = body_len;
    return snake;
}

static Board* make_terminal_first_alive_board(void) {
    Board* board = BoardCreate(5, 5, "duel", 0);
    assert(board != NULL);

    Coord first_body[] = {{2, 2}, {2, 1}, {2, 0}};
    Snake first = make_snake("first", first_body, 3, 90);
    assert(BoardAddSnake(board, &first));
    SnakeFree(&first);
    return board;
}

static Board* make_terminal_second_alive_board(void) {
    Board* board = BoardCreate(5, 5, "duel", 0);
    assert(board != NULL);

    Coord second_body[] = {{2, 2}, {2, 1}, {2, 0}};
    Snake second = make_snake("second", second_body, 3, 90);
    assert(BoardAddSnake(board, &second));
    SnakeFree(&second);
    return board;
}

static Board* make_terminal_first_dead_body_len_zero_board(void) {
    Board* board = BoardCreate(5, 5, "duel", 0);
    assert(board != NULL);

    Coord second_body[] = {{2, 2}, {2, 1}, {2, 0}};
    Snake first = make_snake("first", NULL, 0, 90);
    Snake second = make_snake("second", second_body, 3, 90);
    assert(BoardAddSnake(board, &first));
    assert(BoardAddSnake(board, &second));
    SnakeFree(&first);
    SnakeFree(&second);
    return board;
}

static Board* make_terminal_both_dead_board(void) {
    Board* board = BoardCreate(5, 5, "duel", 0);
    assert(board != NULL);

    Snake first = make_snake("first", NULL, 0, 90);
    Snake second = make_snake("second", NULL, 0, 90);
    assert(BoardAddSnake(board, &first));
    assert(BoardAddSnake(board, &second));
    SnakeFree(&first);
    SnakeFree(&second);
    return board;
}

static Board* make_non_terminal_board(void) {
    Board* board = BoardCreate(5, 5, "duel", 0);
    assert(board != NULL);

    Coord first_body[] = {{2, 2}, {2, 1}, {2, 0}};
    Coord second_body[] = {{0, 0}, {0, 1}, {0, 2}};
    Snake first = make_snake("first", first_body, 3, 90);
    Snake second = make_snake("second", second_body, 3, 90);
    assert(BoardAddSnake(board, &first));
    assert(BoardAddSnake(board, &second));
    SnakeFree(&first);
    SnakeFree(&second);
    return board;
}

static Board* make_length_advantage_board(void) {
    Board* board = BoardCreate(7, 7, "duel", 0);
    assert(board != NULL);

    Coord first_body[] = {{3, 3}, {3, 2}, {3, 1}, {3, 0}};
    Coord second_body[] = {{1, 5}, {1, 4}};
    Snake first = make_snake("first", first_body, 4, 90);
    Snake second = make_snake("second", second_body, 2, 90);

    assert(BoardAddSnake(board, &first));
    assert(BoardAddSnake(board, &second));

    SnakeFree(&first);
    SnakeFree(&second);
    return board;
}

static Board* make_invalid_dimension_board(void) {
    Board* board = BoardCreate(0, 5, "duel", 0);
    assert(board != NULL);

    Coord first_body[] = {{0, 0}, {0, 1}, {0, 2}};
    Coord second_body[] = {{0, 3}, {0, 4}, {0, 2}};
    Snake first = make_snake("first", first_body, 3, 90);
    Snake second = make_snake("second", second_body, 3, 90);

    assert(BoardAddSnake(board, &first));
    assert(BoardAddSnake(board, &second));
    SnakeFree(&first);
    SnakeFree(&second);
    return board;
}

static Board* make_open_duel_board(void) {
    Board* board = BoardCreate(7, 7, "duel", 0);
    assert(board != NULL);

    Coord first_body[] = {{1, 3}, {1, 2}, {1, 1}};
    Coord second_body[] = {{5, 3}, {5, 2}, {5, 1}};
    Snake first = make_snake("first", first_body, 3, 90);
    Snake second = make_snake("second", second_body, 3, 90);

    assert(BoardAddSnake(board, &first));
    assert(BoardAddSnake(board, &second));
    assert(BoardAddFood(board, (Coord){3, 3}));
    assert(BoardAddFood(board, (Coord){2, 5}));
    assert(BoardAddFood(board, (Coord){4, 5}));

    SnakeFree(&first);
    SnakeFree(&second);
    return board;
}

static Board* make_forced_terminal_child_board(void) {
    Board* board = BoardCreate(3, 3, "duel", 0);
    assert(board != NULL);

    Coord first_body[] = {{1, 1}, {1, 0}, {2, 0}, {2, 1}};
    Coord second_body[] = {{0, 1}, {0, 2}, {1, 2}};
    Snake first = make_snake("first", first_body, 4, 90);
    Snake second = make_snake("second", second_body, 3, 90);
    assert(BoardAddSnake(board, &first));
    assert(BoardAddSnake(board, &second));
    SnakeFree(&first);
    SnakeFree(&second);
    return board;
}

static Board* make_head_to_head_risk_board(void) {
    Board* board = BoardCreate(5, 5, "duel", 0);
    assert(board != NULL);

    Coord first_body[] = {{1, 2}, {1, 1}, {1, 0}};
    Coord second_body[] = {{3, 2}, {3, 1}, {3, 0}};
    Snake first = make_snake("first", first_body, 3, 90);
    Snake second = make_snake("second", second_body, 3, 90);
    assert(BoardAddSnake(board, &first));
    assert(BoardAddSnake(board, &second));
    SnakeFree(&first);
    SnakeFree(&second);
    return board;
}

static double stable_sigmoid(double scaled) {
    if (scaled >= 0.0) {
        return 1.0 / (1.0 + exp(-scaled));
    }

    double z = exp(scaled);
    return z / (1.0 + z);
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

void CorePositionEvalTestForceTimeout(bool enabled);
void CorePositionEvalTestForceTimeoutAfterChecks(int checks);
void CorePositionEvalTestForceTimeoutAtRootDepth(int depth);

double CorePositionEvalTestSolveMatrix2x2(double a, double b, double c, double d);
double CorePositionEvalTestSolveMatrix2x2WithConfidence(
    double a,
    double b,
    double c,
    double d,
    double c_ab,
    double c_ac,
    double c_bc,
    double c_bd
);
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
);
CorePositionEvalTestStrategyResult CorePositionEvalTestSolveMatrix4x4Strategies(
    const double values_in[16]
);
#endif

static double expected_heuristic_probability(
    const Board* board,
    const char* first_snake_id,
    const char* second_snake_id,
    const CoreEvaluationWeights* weights
) {
    double first_score = 0.0;
    double second_score = 0.0;
    assert(CoreEvaluateWithWeights(board, first_snake_id, weights, &first_score) == CORE_OK);
    assert(CoreEvaluateWithWeights(board, second_snake_id, weights, &second_score) == CORE_OK);

    return stable_sigmoid((first_score - second_score) / 250.0);
}

static double strategy_sum(const double strategy[4]) {
    return strategy[0] + strategy[1] + strategy[2] + strategy[3];
}

static void assert_strategy_equal(const double left[4], const double right[4]) {
    for (int i = 0; i < 4; i++) {
        assert(fabs(left[i] - right[i]) < 1e-12);
    }
}

static void test_default_config(void) {
    CorePositionEvalConfig config = CorePositionEvalConfigDefault(7000);

    assert(config.time_budget_ms == 7000);
    assert(config.max_depth == 0);
    assert(config.decision_mode == CORE_POSITION_DECISION_MATRIX);
    assert(config.weights.terminal_win == 1000000.0);
}

static void test_terminal_first_alive_is_win(void) {
    Board* board = make_terminal_first_alive_board();
    CorePositionEvalConfig config = CorePositionEvalConfigDefault(1000);
    CorePositionEvalResult result;

    CoreStatus status = CorePositionEvaluateDuel(board, "first", "second", config, &result);

    assert(status == CORE_OK);
    assert(result.first_win_probability == 1.0);
    assert(result.confidence == 1.0);
    assert(result.terminal_leaves == 1);
    assert(result.heuristic_leaves == 0);
    BoardFree(board);
}

static void test_terminal_first_zero_len_is_loss(void) {
    Board* board = make_terminal_first_dead_body_len_zero_board();
    CorePositionEvalConfig config = CorePositionEvalConfigDefault(1000);
    CorePositionEvalResult result;

    CoreStatus status = CorePositionEvaluateDuel(board, "first", "second", config, &result);

    assert(status == CORE_OK);
    assert(result.first_win_probability == 0.0);
    assert(result.confidence == 1.0);
    assert(result.terminal_leaves == 1);
    assert(result.heuristic_leaves == 0);
    BoardFree(board);
}

static void test_terminal_draw_when_both_dead(void) {
    Board* board = make_terminal_both_dead_board();
    CorePositionEvalConfig config = CorePositionEvalConfigDefault(1000);
    CorePositionEvalResult result;

    CoreStatus status = CorePositionEvaluateDuel(board, "first", "second", config, &result);

    assert(status == CORE_OK);
    assert(result.first_win_probability == 0.5);
    assert(result.confidence == 1.0);
    assert(result.terminal_leaves == 1);
    assert(result.heuristic_leaves == 0);
    BoardFree(board);
}

static void test_non_terminal_both_alive_is_heuristic(void) {
    Board* board = make_non_terminal_board();
    CorePositionEvalConfig config = CorePositionEvalConfigDefault(1000);
    CorePositionEvalResult result;

    CoreStatus status = CorePositionEvaluateDuel(board, "first", "second", config, &result);

    assert(status == CORE_OK);
    assert(result.first_win_probability >= 0.0);
    assert(result.first_win_probability <= 1.0);
    assert(result.confidence == 0.0);
    assert(result.terminal_leaves == 0);
    assert(result.heuristic_leaves == 1);
    BoardFree(board);
}

static void test_depth_zero_uses_heuristic_probability(void) {
    Board* board = make_open_duel_board();
    CorePositionEvalConfig config = CorePositionEvalConfigDefault(1000);
    config.max_depth = 0;
    CorePositionEvalResult result;

    CoreStatus status = CorePositionEvaluateDuel(board, "first", "second", config, &result);

    assert(status == CORE_OK);
    double expected = expected_heuristic_probability(
        board,
        "first",
        "second",
        &config.weights
    );
    assert(fabs(result.first_win_probability - expected) < 1e-12);
    assert(result.confidence == 0.0);
    assert(result.terminal_leaves == 0);
    assert(result.heuristic_leaves == 1);
    BoardFree(board);
}

static void test_depth_one_expands_children(void) {
    Board* board = make_open_duel_board();
    CorePositionEvalConfig config = CorePositionEvalConfigDefault(1000);
    config.max_depth = 1;
    config.decision_mode = CORE_POSITION_DECISION_PURE_MINIMAX;
    CorePositionEvalResult result;

    CoreStatus status = CorePositionEvaluateDuel(board, "first", "second", config, &result);

    assert(status == CORE_OK);
    assert(result.nodes > 1);
    assert(result.expanded_children > 0);
    assert(result.heuristic_leaves > 1);
    assert(result.first_win_probability >= 0.0);
    assert(result.first_win_probability <= 1.0);
    assert(result.confidence == 0.0);
    assert(result.completed_depth == 1);
    assert(result.max_depth_started == 1);
    BoardFree(board);
}

static void test_default_matrix_mode_expands_children(void) {
    Board* board = make_open_duel_board();
    CorePositionEvalConfig config = CorePositionEvalConfigDefault(1000);
    config.max_depth = 1;
    CorePositionEvalResult result;

    CoreStatus status = CorePositionEvaluateDuel(board, "first", "second", config, &result);

    assert(status == CORE_OK);
    assert(result.nodes > 1);
    assert(result.expanded_children > 0);
    assert(result.heuristic_leaves > 1);
    assert(result.confidence == 0.0);
    assert(fabs(strategy_sum(result.first_move_probabilities) - 1.0) < 0.000001);
    assert(fabs(strategy_sum(result.second_move_probabilities) - 1.0) < 0.000001);
    assert(result.completed_depth == 1);
    assert(result.max_depth_started == 1);
    BoardFree(board);
}

static void test_matrix_mode_expands_all_command_pairs(void) {
    Board* board = make_head_to_head_risk_board();
    MoveDirection safe_moves[4];
    CorePositionEvalConfig config = CorePositionEvalConfigDefault(1000);
    config.max_depth = 1;
    CorePositionEvalResult result;

    assert(BoardSafeMoves(board, "first", safe_moves) < 4);
    assert(BoardSafeMoves(board, "second", safe_moves) < 4);

    CoreStatus status = CorePositionEvaluateDuel(board, "first", "second", config, &result);

    assert(status == CORE_OK);
    assert(result.nodes == 17);
    assert(result.expanded_children == 16);
    assert(result.terminal_leaves > 0);
    assert(fabs(strategy_sum(result.first_move_probabilities) - 1.0) < 0.000001);
    assert(fabs(strategy_sum(result.second_move_probabilities) - 1.0) < 0.000001);
    BoardFree(board);
}

static void test_unknown_decision_mode_errors(void) {
    Board* board = make_open_duel_board();
    CorePositionEvalConfig config = CorePositionEvalConfigDefault(1000);
    config.max_depth = 1;
    config.decision_mode = (CorePositionDecisionMode)42;
    CorePositionEvalResult result;

    CoreStatus status = CorePositionEvaluateDuel(board, "first", "second", config, &result);

    assert(status == CORE_ERROR);
    BoardFree(board);
}

static void test_forced_timeout_reports_partial_confidence(void) {
    Board* board = make_open_duel_board();
    CorePositionEvalConfig config = CorePositionEvalConfigDefault(1000);
    config.max_depth = 1;
    CorePositionEvalResult result;

    CorePositionEvalTestForceTimeoutAfterChecks(2);
    CoreStatus status = CorePositionEvaluateDuel(board, "first", "second", config, &result);
    CorePositionEvalTestForceTimeoutAfterChecks(-1);

    assert(status == CORE_OK);
    assert(result.timed_out == true);
    assert(result.timeout_leaves > 0);
    /* Depth 1 could not finish, so the result falls back to depth 0. */
    assert(result.completed_depth == 0);
    assert(result.max_depth_started == 1);
    assert(result.nodes > 1);
    assert(result.expanded_children > 0);
    assert(result.first_win_probability >= 0.0);
    assert(result.first_win_probability <= 1.0);
    assert(result.confidence == 0.0);
    assert(strategy_sum(result.first_move_probabilities) == 0.0);
    assert(strategy_sum(result.second_move_probabilities) == 0.0);
    assert(result.heuristic_leaves > 0);
    BoardFree(board);
}

static void test_forced_terminal_child_reaches_terminal_leaf(void) {
    Board* board = make_forced_terminal_child_board();
    CorePositionEvalConfig config = CorePositionEvalConfigDefault(1000);
    config.max_depth = 2;
    CorePositionEvalResult result;
    const Snake* first = BoardFindSnakeConst(board, "first");
    const Snake* second = BoardFindSnakeConst(board, "second");

    assert(first != NULL);
    assert(first->body_len > 0);
    assert(second != NULL);
    assert(second->body_len > 0);

    CoreStatus status = CorePositionEvaluateDuel(board, "first", "second", config, &result);

    assert(status == CORE_OK);
    assert(result.confidence >= 0.0);
    assert(result.confidence <= 1.0);
    assert(result.nodes > 1);
    assert(result.expanded_children > 0);
    assert(result.terminal_leaves > 0);
    BoardFree(board);
}

static void test_extreme_weights_stays_finite_and_near_boundary(void) {
    Board* board = make_length_advantage_board();
    CorePositionEvalConfig config = CorePositionEvalConfigDefault(1000);
    config.weights.base = 0.0;
    config.weights.health = 0.0;
    config.weights.length = 1e9;
    config.weights.reachable_space = 0.0;
    config.weights.safe_moves = 0.0;
    config.weights.center = 0.0;
    config.weights.food = 0.0;
    config.weights.low_health_food = 0.0;
    config.weights.low_health_threshold = 0.0;
    config.weights.hazard_damage = 0.0;
    config.weights.hazard = 0.0;
    config.weights.length_advantage = 0.0;
    config.weights.adjacent_equal_or_longer_penalty = 0.0;
    config.weights.adjacent_shorter_bonus = 0.0;
    config.weights.opponent_reachable_space = 0.0;
    config.weights.territory_delta = 0.0;
    config.weights.opponent_safe_moves = 0.0;
    config.weights.opponent_low_health_food_denial = 0.0;
    CorePositionEvalResult result;

    CoreStatus status = CorePositionEvaluateDuel(board, "first", "second", config, &result);

    assert(status == CORE_OK);
    assert(isfinite(result.first_win_probability));
    assert(result.first_win_probability >= 0.0);
    assert(result.first_win_probability <= 1.0);
    assert(result.first_win_probability > 0.999999);

    double expected = expected_heuristic_probability(
        board,
        "first",
        "second",
        &config.weights
    );
    assert(fabs(result.first_win_probability - expected) < 1e-12);
    BoardFree(board);
}

static void test_non_finite_weight_is_sanitized_to_probability_fallback(void) {
    Board* board = make_open_duel_board();
    CorePositionEvalConfig config = CorePositionEvalConfigDefault(1000);
    config.weights.length = NAN;
    CorePositionEvalResult result;

    CoreStatus status = CorePositionEvaluateDuel(board, "first", "second", config, &result);

    assert(status == CORE_OK);
    assert(isfinite(result.first_win_probability));
    assert(result.first_win_probability == 0.5);
    assert(result.confidence == 0.0);
    assert(result.terminal_leaves == 0);
    assert(result.heuristic_leaves == 1);
    BoardFree(board);
}

static void test_heuristic_error_does_not_increment_heuristic_leaves(void) {
    Board* board = make_invalid_dimension_board();
    CorePositionEvalConfig config = CorePositionEvalConfigDefault(1000);
    CorePositionEvalResult result = {
        .first_win_probability = 123.0,
        .confidence = 123.0,
        .nodes = 123,
        .terminal_leaves = 123,
        .heuristic_leaves = 123,
        .timeout_leaves = 123,
        .expanded_children = 123,
        .timed_out = true,
        .elapsed_ms = 123.0,
    };

    CoreStatus status = CorePositionEvaluateDuel(board, "first", "second", config, &result);

    assert(status == CORE_ERROR);
    assert(result.first_win_probability == 0.0);
    assert(result.confidence == 0.0);
    assert(result.nodes == 0);
    assert(result.terminal_leaves == 0);
    assert(result.heuristic_leaves == 0);
    assert(result.timeout_leaves == 0);
    assert(result.expanded_children == 0);
    assert(result.timed_out == false);
    assert(result.elapsed_ms == 0.0);
    BoardFree(board);
}

static void test_iterative_deepening_keeps_last_complete_root_policy(void) {
    Board* board = make_open_duel_board();

    CorePositionEvalConfig baseline_config = CorePositionEvalConfigDefault(1000);
    baseline_config.max_depth = 1;
    CorePositionEvalResult baseline;
    CoreStatus baseline_status = CorePositionEvaluateDuel(
        board,
        "first",
        "second",
        baseline_config,
        &baseline
    );
    assert(baseline_status == CORE_OK);
    assert(baseline.completed_depth == 1);
    assert(baseline.max_depth_started == 1);
    assert(fabs(strategy_sum(baseline.first_move_probabilities) - 1.0) < 0.000001);
    assert(fabs(strategy_sum(baseline.second_move_probabilities) - 1.0) < 0.000001);

    CorePositionEvalConfig config = CorePositionEvalConfigDefault(1000);
    config.max_depth = 3;
    CorePositionEvalResult result;

    CorePositionEvalTestForceTimeoutAtRootDepth(2);
    CoreStatus status = CorePositionEvaluateDuel(board, "first", "second", config, &result);
    CorePositionEvalTestForceTimeoutAtRootDepth(-1);

    assert(status == CORE_OK);
    assert(result.timed_out == true);
    assert(result.completed_depth == 1);
    assert(result.max_depth_started == 2);
    assert(fabs(result.first_win_probability - baseline.first_win_probability) < 1e-12);
    assert(fabs(result.confidence - baseline.confidence) < 1e-12);
    assert_strategy_equal(result.first_move_probabilities, baseline.first_move_probabilities);
    assert_strategy_equal(result.second_move_probabilities, baseline.second_move_probabilities);
    assert(result.timeout_leaves > 0);
    assert(result.expanded_children >= baseline.expanded_children);
    BoardFree(board);
}

static void test_root_timeout_before_depth_one_returns_depth_zero_heuristic(void) {
    Board* board = make_open_duel_board();
    CorePositionEvalConfig config = CorePositionEvalConfigDefault(1000);
    config.max_depth = 2;
    CorePositionEvalResult result;

    CorePositionEvalTestForceTimeoutAtRootDepth(1);
    CoreStatus status = CorePositionEvaluateDuel(board, "first", "second", config, &result);
    CorePositionEvalTestForceTimeoutAtRootDepth(-1);

    assert(status == CORE_OK);
    assert(result.timed_out == true);
    assert(result.completed_depth == 0);
    assert(result.max_depth_started == 1);
    assert(result.timeout_leaves > 0);
    assert(result.heuristic_leaves > 0);

    double expected = expected_heuristic_probability(
        board,
        "first",
        "second",
        &config.weights
    );
    assert(fabs(result.first_win_probability - expected) < 1e-12);
    assert(result.confidence == 0.0);
    assert(strategy_sum(result.first_move_probabilities) == 0.0);
    assert(strategy_sum(result.second_move_probabilities) == 0.0);
    BoardFree(board);
}

static void test_matrix_solver_matches_matching_pennies(void) {
    double value = CorePositionEvalTestSolveMatrix2x2(1.0, 0.0, 0.0, 1.0);
    assert(fabs(value - 0.5) < 0.000001);
}

static void test_matrix_solver_picks_dominant_row(void) {
    double value = CorePositionEvalTestSolveMatrix2x2(0.8, 0.7, 0.3, 0.2);
    assert(fabs(value - 0.7) < 0.000001);
}

static void test_matrix_solver_returns_mixed_4x4_strategy(void) {
    const double values[16] = {
        1.0, 0.0, 0.6, 0.6,
        0.0, 1.0, 0.6, 0.6,
        0.4, 0.4, 0.4, 0.4,
        0.3, 0.3, 0.3, 0.3,
    };
    CorePositionEvalTestStrategyResult result = CorePositionEvalTestSolveMatrix4x4Strategies(values);

    assert(fabs(result.probability - 0.5) < 0.000001);
    assert(fabs(result.row_strategy[0] - 0.5) < 0.000001);
    assert(fabs(result.row_strategy[1] - 0.5) < 0.000001);
    assert(result.row_strategy[2] < 0.000001);
    assert(result.row_strategy[3] < 0.000001);
    assert(fabs(result.col_strategy[0] - 0.5) < 0.000001);
    assert(fabs(result.col_strategy[1] - 0.5) < 0.000001);
    assert(result.col_strategy[2] < 0.000001);
    assert(result.col_strategy[3] < 0.000001);
}

static void test_matrix_confidence_prefers_higher_confidence_tie(void) {
    double confidence = CorePositionEvalTestSolveMatrix2x2WithConfidence(
        0.5,
        0.5,
        0.5,
        0.5,
        0.1,
        0.2,
        0.9,
        0.8
    );
    assert(fabs(confidence - 0.9) < 0.000001);
}

static void test_matrix_confidence_tie_is_order_insensitive(void) {
    double base = CorePositionEvalTestSolveMatrix2x2WithConfidence(
        0.5,
        0.5,
        0.5,
        0.5,
        0.1,
        0.9,
        0.8,
        0.7
    );
    double row_swapped = CorePositionEvalTestSolveMatrix2x2WithConfidence(
        0.5,
        0.5,
        0.5,
        0.5,
        0.8,
        0.7,
        0.1,
        0.9
    );
    double col_swapped = CorePositionEvalTestSolveMatrix2x2WithConfidence(
        0.5,
        0.5,
        0.5,
        0.5,
        0.9,
        0.1,
        0.7,
        0.8
    );
    assert(fabs(base - 0.9) < 0.000001);
    assert(fabs(row_swapped - 0.9) < 0.000001);
    assert(fabs(col_swapped - 0.9) < 0.000001);
}

static void test_timeout_backup_preserves_evaluated_cells(void) {
    CorePositionEvalTestTimeoutBackupResult preserved = CorePositionEvalTestFillTimeoutMatrix2x2(
        0.9,
        0.2,
        0.0,
        1.0,
        0.95,
        0.0,
        0.0,
        0.0,
        true,
        false,
        false,
        true,
        0.2
    );

    assert(fabs(preserved.probability - 0.5733333333333334) < 0.000001);
    assert(fabs(preserved.confidence - 0.2702222222222222) < 0.000001);
}

static void test_terminal_second_alive_is_loss(void) {
    Board* board = make_terminal_second_alive_board();
    CorePositionEvalConfig config = CorePositionEvalConfigDefault(1000);
    CorePositionEvalResult result;

    CoreStatus status = CorePositionEvaluateDuel(board, "first", "second", config, &result);

    assert(status == CORE_OK);
    assert(result.first_win_probability == 0.0);
    assert(result.confidence == 1.0);
    assert(result.terminal_leaves == 1);
    assert(result.heuristic_leaves == 0);
    BoardFree(board);
}

int main(void) {
    test_default_config();
    test_terminal_first_alive_is_win();
    test_terminal_first_zero_len_is_loss();
    test_terminal_draw_when_both_dead();
    test_non_terminal_both_alive_is_heuristic();
    test_terminal_second_alive_is_loss();
    test_depth_zero_uses_heuristic_probability();
    test_depth_one_expands_children();
    test_default_matrix_mode_expands_children();
    test_matrix_mode_expands_all_command_pairs();
    test_unknown_decision_mode_errors();
    test_forced_timeout_reports_partial_confidence();
    test_forced_terminal_child_reaches_terminal_leaf();
    test_extreme_weights_stays_finite_and_near_boundary();
    test_non_finite_weight_is_sanitized_to_probability_fallback();
    test_heuristic_error_does_not_increment_heuristic_leaves();
    test_iterative_deepening_keeps_last_complete_root_policy();
    test_root_timeout_before_depth_one_returns_depth_zero_heuristic();
    test_matrix_solver_matches_matching_pennies();
    test_matrix_solver_picks_dominant_row();
    test_matrix_solver_returns_mixed_4x4_strategy();
    test_matrix_confidence_prefers_higher_confidence_tie();
    test_matrix_confidence_tie_is_order_insensitive();
    test_timeout_backup_preserves_evaluated_cells();
    puts("position_eval C tests passed");
    return 0;
}
