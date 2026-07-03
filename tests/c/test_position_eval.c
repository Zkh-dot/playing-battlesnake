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
    assert(result.first_win_probability == 0.5);
    assert(result.confidence == 0.0);
    assert(result.terminal_leaves == 0);
    assert(result.heuristic_leaves == 1);
    BoardFree(board);
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
    puts("position_eval C tests passed");
    return 0;
}
