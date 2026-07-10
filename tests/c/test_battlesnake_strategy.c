#include "../../battlesnake/c-core/server/battlesnake_strategy.h"
#include "../../battlesnake/c-core/core/core_algorithms.h"

#include <assert.h>
#include <stdio.h>
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

static void test_standard_three_snakes_uses_safe_fallback_until_native_parity_graduates(void) {
    Board* board = BoardCreate(7, 7, "standard", 0);
    Coord me_body[] = {{2, 2}, {2, 1}, {2, 0}};
    Coord north_body[] = {{6, 6}, {6, 5}, {6, 4}};
    Coord east_body[] = {{6, 0}, {5, 0}, {4, 0}};
    Snake me = make_snake("me", me_body, 3, 90);
    Snake north = make_snake("north", north_body, 3, 90);
    Snake east = make_snake("east", east_body, 3, 90);
    MoveDirection move = MOVE_INVALID;
    BsStrategyConfig config = BsStrategyConfigDefault();

    config.default_time_budget_ms = 80;
    assert(BoardAddSnake(board, &me));
    assert(BoardAddSnake(board, &north));
    assert(BoardAddSnake(board, &east));
    assert(BsChooseMove(board, "me", &config, &move) == BS_STRATEGY_FALLBACK_USED);
    assert(move == MOVE_UP || move == MOVE_DOWN || move == MOVE_LEFT || move == MOVE_RIGHT);

    SnakeFree(&me);
    SnakeFree(&north);
    SnakeFree(&east);
    BoardFree(board);
}

static void test_standard_multi_snake_malformed_body_uses_fallback(void) {
    Board* board = BoardCreate(7, 7, "standard", 0);
    Coord north_body[] = {{6, 6}, {6, 5}, {6, 4}};
    Coord east_body[] = {{6, 0}, {5, 0}, {4, 0}};
    Snake me = make_snake("me", NULL, 0, 90);
    Snake north = make_snake("north", north_body, 3, 90);
    Snake east = make_snake("east", east_body, 3, 90);
    MoveDirection move = MOVE_INVALID;
    BsStrategyConfig config = BsStrategyConfigDefault();

    assert(BoardAddSnake(board, &me));
    assert(BoardAddSnake(board, &north));
    assert(BoardAddSnake(board, &east));
    assert(BsChooseMove(board, "me", &config, &move) == BS_STRATEGY_FALLBACK_USED);
    assert(move == MOVE_UP);

    SnakeFree(&me);
    SnakeFree(&north);
    SnakeFree(&east);
    BoardFree(board);
}

static void test_standard_multi_snake_excess_snakes_returns_legal_safe_move(void) {
    Board* board = BoardCreate(7, 7, "standard", 0);
    Coord me_body[] = {{2, 2}, {2, 1}, {2, 0}};
    Snake me = make_snake("me", me_body, 3, 90);
    MoveDirection move = MOVE_INVALID;
    BsStrategyConfig config = BsStrategyConfigDefault();

    assert(BoardAddSnake(board, &me));
    for (int i = 0; i < 9; i++) {
        Coord body[] = {{i % 7, 6}, {i % 7, 5}};
        char id[16];
        snprintf(id, sizeof(id), "other-%d", i);
        Snake other = make_snake(id, body, 2, 90);
        assert(BoardAddSnake(board, &other));
        SnakeFree(&other);
    }

    assert(BsChooseMove(board, "me", &config, &move) == BS_STRATEGY_FALLBACK_USED);
    assert(move == MOVE_UP || move == MOVE_DOWN || move == MOVE_LEFT || move == MOVE_RIGHT);

    SnakeFree(&me);
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

static void test_duel_root_profile_prefers_contingent_survival_over_terminal_moves(void) {
    Board* board = BoardCreate(11, 11, "standard", 0);
    Coord me_body[] = {{6, 10}, {7, 10}, {7, 9}, {6, 9}};
    Coord you_body[] = {{5, 9}, {5, 8}, {5, 7}, {5, 6}};
    Snake me = make_snake("me", me_body, 4, 96);
    Snake you = make_snake("you", you_body, 4, 96);
    CoreDuelRootProfileResult profile;
    MoveDirection move = MOVE_INVALID;
    BsStrategyConfig config = BsStrategyConfigDefault();

    config.default_time_budget_ms = 50;
    assert(BoardAddSnake(board, &me));
    assert(BoardAddSnake(board, &you));
    assert(BoardSafeMoves(board, "me", (MoveDirection[4]){MOVE_INVALID}) == 0);
    assert(CoreDuelRootProfile(board, "me", &profile) == CORE_OK);
    assert(profile.commands[MOVE_UP].alive_reply_count == 0);
    assert((profile.commands[MOVE_UP].immediate_causes & CORE_TERMINAL_CAUSE_WALL) != 0);
    assert(profile.commands[MOVE_RIGHT].alive_reply_count == 0);
    assert((profile.commands[MOVE_RIGHT].immediate_causes & CORE_TERMINAL_CAUSE_SELF_BODY) != 0);
    assert(profile.commands[MOVE_DOWN].alive_reply_count > 0 || profile.commands[MOVE_LEFT].alive_reply_count > 0);
    assert(BsChooseMove(board, "me", &config, &move) == BS_STRATEGY_OK);
    assert(move == MOVE_DOWN || move == MOVE_LEFT);

    SnakeFree(&me);
    SnakeFree(&you);
    BoardFree(board);
}

static void test_default_search_config_uses_ladder_policy(void) {
    CoreSearchConfig config = CoreSearchConfigDefault(50);
    CoreSearchConfig zeroed;
    memset(&zeroed, 0, sizeof(zeroed));

    assert(config.root_policy == CORE_ROOT_POLICY_STANDARD_LADDER_OPPORTUNITY);
    assert(zeroed.root_policy == CORE_ROOT_POLICY_STRICT_MINIMAX);
}

int main(void) {
    test_single_snake_uses_safe_fallback();
    test_missing_snake_is_error();
    test_solo_two_snakes_uses_minimax();
    test_solo_two_snakes_uses_minimax_with_null_config();
    test_standard_two_snakes_uses_minimax();
    test_standard_three_snakes_uses_safe_fallback_until_native_parity_graduates();
    test_standard_multi_snake_malformed_body_uses_fallback();
    test_standard_multi_snake_excess_snakes_returns_legal_safe_move();
    test_null_config_uses_default_budget_and_fallback();
    test_effective_budget_uses_configured_budget_without_request_timeout();
    test_effective_budget_clamps_to_request_timeout_margin();
    test_effective_budget_preserves_smaller_env_budget();
    test_effective_budget_floors_tiny_request_timeout();
    test_effective_budget_allows_zero_margin();
    test_duel_root_profile_prefers_contingent_survival_over_terminal_moves();
    test_default_search_config_uses_ladder_policy();
    return 0;
}
