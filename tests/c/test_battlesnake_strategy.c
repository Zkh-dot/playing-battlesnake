#include "../../battlesnake/c-core/server/battlesnake_strategy.h"

#include <assert.h>
#include <string.h>

static Snake make_snake(const char* id, Coord* body, int body_len, int health) {
    Snake snake;
    SnakeInit(&snake, id, id, health, body, body_len);
    snake.length = body_len;
    return snake;
}

static void test_single_snake_uses_safe_fallback(void) {
    Board* board = BoardCreate(5, 5, "standard", 0);
    Coord body[] = {{2, 2}, {2, 1}, {2, 0}};
    Snake snake = make_snake("me", body, 3, 90);
    MoveDirection move = MOVE_INVALID;
    BsStrategyConfig config = BsStrategyConfigDefault();

    assert(BoardAddSnake(board, &snake));
    assert(BsChooseMove(board, "me", &config, &move) == BS_STRATEGY_FALLBACK_USED);
    assert(move == MOVE_UP || move == MOVE_DOWN || move == MOVE_LEFT || move == MOVE_RIGHT);

    SnakeFree(&snake);
    BoardFree(board);
}

static void test_missing_snake_is_error(void) {
    Board* board = BoardCreate(5, 5, "standard", 0);
    MoveDirection move = MOVE_UP;
    BsStrategyConfig config = BsStrategyConfigDefault();

    assert(BsChooseMove(board, "me", &config, &move) == BS_STRATEGY_ERROR);
    assert(move == MOVE_INVALID);

    BoardFree(board);
}

static void test_solo_two_snakes_uses_minimax(void) {
    Board* board = BoardCreate(7, 7, "solo", 0);
    Coord me_body[] = {{1, 3}, {1, 2}, {1, 1}};
    Coord you_body[] = {{5, 3}, {5, 2}, {5, 1}};
    Snake me = make_snake("me", me_body, 3, 90);
    Snake you = make_snake("you", you_body, 3, 90);
    MoveDirection move = MOVE_INVALID;
    BsStrategyConfig config = BsStrategyConfigDefault();

    config.default_time_budget_ms = 25;
    assert(BoardAddSnake(board, &me));
    assert(BoardAddSnake(board, &you));
    assert(BoardAddFood(board, (Coord){3, 3}));
    assert(BsChooseMove(board, "me", &config, &move) == BS_STRATEGY_OK);
    assert(move == MOVE_UP || move == MOVE_DOWN || move == MOVE_LEFT || move == MOVE_RIGHT);

    SnakeFree(&me);
    SnakeFree(&you);
    BoardFree(board);
}

static void test_solo_two_snakes_uses_minimax_with_null_config(void) {
    Board* board = BoardCreate(7, 7, "solo", 0);
    Coord me_body[] = {{1, 3}, {1, 2}, {1, 1}};
    Coord you_body[] = {{5, 3}, {5, 2}, {5, 1}};
    Snake me = make_snake("me", me_body, 3, 90);
    Snake you = make_snake("you", you_body, 3, 90);
    MoveDirection move = MOVE_INVALID;

    assert(BoardAddSnake(board, &me));
    assert(BoardAddSnake(board, &you));
    assert(BoardAddFood(board, (Coord){3, 3}));
    assert(BsChooseMove(board, "me", 0, &move) == BS_STRATEGY_OK);
    assert(move == MOVE_UP || move == MOVE_DOWN || move == MOVE_LEFT || move == MOVE_RIGHT);

    SnakeFree(&me);
    SnakeFree(&you);
    BoardFree(board);
}

static void test_standard_two_snakes_uses_minimax(void) {
    Board* board = BoardCreate(7, 7, "standard", 0);
    Coord me_body[] = {{1, 3}, {1, 2}, {1, 1}};
    Coord you_body[] = {{5, 3}, {5, 2}, {5, 1}};
    Snake me = make_snake("me", me_body, 3, 90);
    Snake you = make_snake("you", you_body, 3, 90);
    MoveDirection move = MOVE_INVALID;
    BsStrategyConfig config = BsStrategyConfigDefault();

    config.default_time_budget_ms = 25;
    assert(BoardAddSnake(board, &me));
    assert(BoardAddSnake(board, &you));
    assert(BoardAddFood(board, (Coord){3, 3}));
    assert(BsChooseMove(board, "me", &config, &move) == BS_STRATEGY_OK);
    assert(move == MOVE_UP || move == MOVE_DOWN || move == MOVE_LEFT || move == MOVE_RIGHT);

    SnakeFree(&me);
    SnakeFree(&you);
    BoardFree(board);
}

static void test_null_config_uses_default_budget_and_fallback(void) {
    Board* board = BoardCreate(5, 5, "standard", 0);
    Coord body[] = {{2, 2}, {2, 1}, {2, 0}};
    Snake snake = make_snake("me", body, 3, 90);
    MoveDirection safe_moves[4];
    MoveDirection move = MOVE_INVALID;

    assert(BoardAddSnake(board, &snake));
    int safe_count = BoardSafeMoves(board, "me", safe_moves);
    assert(BsChooseMove(board, "me", 0, &move) == BS_STRATEGY_FALLBACK_USED);
    if (safe_count > 0) {
        assert(move == safe_moves[0]);
    } else {
        assert(move == MOVE_UP);
    }

    SnakeFree(&snake);
    BoardFree(board);
}

static void test_effective_budget_uses_configured_budget_without_request_timeout(void) {
    BsStrategyConfig config = BsStrategyConfigDefault();

    config.default_time_budget_ms = 275;
    assert(BsStrategyEffectiveBudgetMs(&config) == 275);
}

static void test_effective_budget_clamps_to_request_timeout_margin(void) {
    BsStrategyConfig config = BsStrategyConfigDefault();

    config.default_time_budget_ms = 400;
    config.game_timeout_ms = 500;
    config.safety_margin_ms = 150;
    config.min_time_budget_ms = 50;
    assert(BsStrategyEffectiveBudgetMs(&config) == 350);
}

static void test_effective_budget_preserves_smaller_env_budget(void) {
    BsStrategyConfig config = BsStrategyConfigDefault();

    config.default_time_budget_ms = 25;
    config.game_timeout_ms = 500;
    config.safety_margin_ms = 150;
    config.min_time_budget_ms = 50;
    assert(BsStrategyEffectiveBudgetMs(&config) == 25);
}

static void test_effective_budget_floors_tiny_request_timeout(void) {
    BsStrategyConfig config = BsStrategyConfigDefault();

    config.default_time_budget_ms = 400;
    config.game_timeout_ms = 100;
    config.safety_margin_ms = 150;
    config.min_time_budget_ms = 50;
    assert(BsStrategyEffectiveBudgetMs(&config) == 50);
}

static void test_effective_budget_allows_zero_margin(void) {
    BsStrategyConfig config = BsStrategyConfigDefault();

    config.default_time_budget_ms = 400;
    config.game_timeout_ms = 500;
    config.safety_margin_ms = 0;
    config.min_time_budget_ms = 50;
    assert(BsStrategyEffectiveBudgetMs(&config) == 400);
}

int main(void) {
    test_single_snake_uses_safe_fallback();
    test_missing_snake_is_error();
    test_solo_two_snakes_uses_minimax();
    test_solo_two_snakes_uses_minimax_with_null_config();
    test_standard_two_snakes_uses_minimax();
    test_null_config_uses_default_budget_and_fallback();
    test_effective_budget_uses_configured_budget_without_request_timeout();
    test_effective_budget_clamps_to_request_timeout_margin();
    test_effective_budget_preserves_smaller_env_budget();
    test_effective_budget_floors_tiny_request_timeout();
    test_effective_budget_allows_zero_margin();
    return 0;
}
