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

static CoreSearchValue root_test_value(
    CoreOutcome outcome,
    CoreValueBound bound,
    double score,
    uint16_t terminal_distance
) {
    CoreSearchValue value;
    memset(&value, 0, sizeof(value));
    value.outcome = outcome;
    value.bound = bound;
    value.score = score;
    value.terminal_distance = terminal_distance;
    return value;
}

static CoreRootCandidateStats root_test_stats(
    CoreStructuralProofResult proof,
    int relaxed_static_capacity,
    int post_move_length
) {
    CoreRootCandidateStats stats;
    memset(&stats, 0, sizeof(stats));
    stats.structural_proof = proof;
    stats.relaxed_static_capacity = relaxed_static_capacity;
    stats.post_move_length = post_move_length;
    return stats;
}

static void assert_root_comparison(
    CoreSearchValue candidate_value,
    CoreRootCandidateStats candidate_stats,
    CoreSearchValue incumbent_value,
    CoreRootCandidateStats incumbent_stats,
    CoreRootComparisonOrdering expected_ordering,
    CoreRootComparisonReason expected_reason
) {
    CoreRootComparison comparison = CoreCompareRootCandidates(
        &candidate_value,
        &candidate_stats,
        &incumbent_value,
        &incumbent_stats
    );
    assert(comparison.ordering == expected_ordering);
    assert(comparison.reason == expected_reason);
    CoreRootComparison reverse = CoreCompareRootCandidates(
        &incumbent_value,
        &incumbent_stats,
        &candidate_value,
        &candidate_stats
    );
    CoreRootComparisonOrdering expected_reverse = expected_ordering;
    if (expected_ordering == CORE_ROOT_COMPARISON_CANDIDATE) {
        expected_reverse = CORE_ROOT_COMPARISON_INCUMBENT;
    } else if (expected_ordering == CORE_ROOT_COMPARISON_INCUMBENT) {
        expected_reverse = CORE_ROOT_COMPARISON_CANDIDATE;
    }
    assert(reverse.ordering == expected_reverse);
    assert(reverse.reason == expected_reason);
}

static void test_root_comparison_orders_exact_outcomes_before_structure(void) {
    const CoreOutcome better[] = {
        CORE_OUTCOME_WIN,
        CORE_OUTCOME_DRAW,
        CORE_OUTCOME_UNRESOLVED,
    };
    const CoreOutcome worse[] = {
        CORE_OUTCOME_DRAW,
        CORE_OUTCOME_UNRESOLVED,
        CORE_OUTCOME_LOSS,
    };
    CoreRootCandidateStats safe = root_test_stats(CORE_STRUCTURAL_PROOF_SAFE, 8, 3);
    CoreRootCandidateStats unsafe = root_test_stats(CORE_STRUCTURAL_PROOF_UNSAFE, 0, 3);

    for (size_t index = 0; index < sizeof(better) / sizeof(better[0]); index++) {
        assert_root_comparison(
            root_test_value(better[index], CORE_VALUE_BOUND_EXACT, 0.0, 0),
            unsafe,
            root_test_value(worse[index], CORE_VALUE_BOUND_EXACT, 0.0, 0),
            safe,
            CORE_ROOT_COMPARISON_CANDIDATE,
            CORE_ROOT_COMPARISON_TERMINAL_OUTCOME
        );
    }

    assert_root_comparison(
        root_test_value(CORE_OUTCOME_DRAW, CORE_VALUE_BOUND_EXACT, 0.0, 0),
        unsafe,
        root_test_value(CORE_OUTCOME_LOSS, CORE_VALUE_BOUND_EXACT, 1000000.0, 20),
        safe,
        CORE_ROOT_COMPARISON_CANDIDATE,
        CORE_ROOT_COMPARISON_TERMINAL_OUTCOME
    );
}

static void test_root_comparison_uses_only_decisive_search_bounds(void) {
    CoreRootCandidateStats equal_structure = root_test_stats(
        CORE_STRUCTURAL_PROOF_NOT_ANALYZED,
        0,
        0
    );

    assert_root_comparison(
        root_test_value(CORE_OUTCOME_DRAW, CORE_VALUE_BOUND_LOWER, 0.0, 0),
        equal_structure,
        root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_EXACT, 100.0, 0),
        equal_structure,
        CORE_ROOT_COMPARISON_CANDIDATE,
        CORE_ROOT_COMPARISON_SEARCH_BOUND
    );
    assert_root_comparison(
        root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_EXACT, -100.0, 0),
        equal_structure,
        root_test_value(CORE_OUTCOME_LOSS, CORE_VALUE_BOUND_UPPER, 100.0, 0),
        equal_structure,
        CORE_ROOT_COMPARISON_CANDIDATE,
        CORE_ROOT_COMPARISON_SEARCH_BOUND
    );
    assert_root_comparison(
        root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_UPPER, -10.0, 0),
        equal_structure,
        root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_LOWER, 10.0, 0),
        equal_structure,
        CORE_ROOT_COMPARISON_EQUAL,
        CORE_ROOT_COMPARISON_NOT_COMPARED
    );
}

static void test_root_comparison_applies_structural_lattice_to_unresolved_values(void) {
    CoreSearchValue structurally_preferred_value = root_test_value(
        CORE_OUTCOME_UNRESOLVED,
        CORE_VALUE_BOUND_EXACT,
        -100.0,
        0
    );
    CoreSearchValue heuristic_preferred_value = root_test_value(
        CORE_OUTCOME_UNRESOLVED,
        CORE_VALUE_BOUND_EXACT,
        100.0,
        0
    );
    CoreRootCandidateStats safe = root_test_stats(CORE_STRUCTURAL_PROOF_SAFE, 2, 4);
    CoreRootCandidateStats unsafe = root_test_stats(CORE_STRUCTURAL_PROOF_UNSAFE, 20, 4);
    CoreRootCandidateStats deficient_unknown = root_test_stats(
        CORE_STRUCTURAL_PROOF_UNKNOWN,
        3,
        4
    );
    CoreRootCandidateStats sufficient_unknown = root_test_stats(
        CORE_STRUCTURAL_PROOF_UNKNOWN,
        4,
        4
    );

    assert_root_comparison(
        structurally_preferred_value,
        safe,
        heuristic_preferred_value,
        unsafe,
        CORE_ROOT_COMPARISON_CANDIDATE,
        CORE_ROOT_COMPARISON_STRUCTURAL_PROOF
    );
    assert_root_comparison(
        structurally_preferred_value,
        safe,
        heuristic_preferred_value,
        deficient_unknown,
        CORE_ROOT_COMPARISON_CANDIDATE,
        CORE_ROOT_COMPARISON_STRUCTURAL_PROOF
    );
    assert_root_comparison(
        structurally_preferred_value,
        safe,
        heuristic_preferred_value,
        sufficient_unknown,
        CORE_ROOT_COMPARISON_INCUMBENT,
        CORE_ROOT_COMPARISON_HEURISTIC_VALUE
    );
    assert_root_comparison(
        structurally_preferred_value,
        safe,
        structurally_preferred_value,
        sufficient_unknown,
        CORE_ROOT_COMPARISON_EQUAL,
        CORE_ROOT_COMPARISON_NOT_COMPARED
    );
}

static void test_root_comparison_prefers_later_exact_loss(void) {
    CoreRootCandidateStats equal_structure = root_test_stats(
        CORE_STRUCTURAL_PROOF_NOT_ANALYZED,
        0,
        0
    );

    assert_root_comparison(
        root_test_value(CORE_OUTCOME_LOSS, CORE_VALUE_BOUND_EXACT, -999999.0, 9),
        equal_structure,
        root_test_value(CORE_OUTCOME_LOSS, CORE_VALUE_BOUND_EXACT, -999990.0, 4),
        equal_structure,
        CORE_ROOT_COMPARISON_CANDIDATE,
        CORE_ROOT_COMPARISON_TERMINAL_SURVIVAL
    );
}

static void test_root_comparison_uses_heuristic_only_after_structural_equality(void) {
    CoreRootCandidateStats structure = root_test_stats(CORE_STRUCTURAL_PROOF_SAFE, 12, 4);

    assert_root_comparison(
        root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_EXACT, 17.0, 0),
        structure,
        root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_EXACT, 16.0, 0),
        structure,
        CORE_ROOT_COMPARISON_CANDIDATE,
        CORE_ROOT_COMPARISON_HEURISTIC_VALUE
    );
    assert_root_comparison(
        root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_EXACT, 17.0, 0),
        structure,
        root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_EXACT, 17.0, 0),
        structure,
        CORE_ROOT_COMPARISON_EQUAL,
        CORE_ROOT_COMPARISON_NOT_COMPARED
    );
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

static void test_branching_pocket_proves_every_branch_dies(void) {
    Board* board = BoardCreate(5, 5, "standard", 0);
    Coord me_body[] = {{2, 0}, {2, 1}, {2, 2}, {1, 2}, {0, 2}, {0, 3}, {1, 3}, {1, 4}, {0, 4}};
    Coord you_body[] = {{4, 4}, {4, 3}, {4, 2}};
    Snake me = make_snake("me", me_body, 9, 90);
    Snake you = make_snake("you", you_body, 3, 90);
    CoreSearchConfig config = CoreSearchConfigDefault(1000);
    CoreSearchStats stats;
    MoveDirection move = MOVE_INVALID;

    config.fixed_depth = 1;
    assert(BoardAddSnake(board, &me));
    assert(BoardAddSnake(board, &you));
    assert(CoreMinimaxMoveWithStats(board, "me", config, &move, &stats) == CORE_OK);
    assert(stats.root_candidates[MOVE_LEFT].structural_proof == CORE_STRUCTURAL_PROOF_UNSAFE);
    assert(stats.root_candidates[MOVE_LEFT].proof_cutoff == CORE_STRUCTURAL_CUTOFF_DEAD_END);
    assert(stats.root_candidates[MOVE_LEFT].proof_horizon == 34);
    assert(stats.root_candidates[MOVE_LEFT].explored_states > 1);

    SnakeFree(&me);
    SnakeFree(&you);
    BoardFree(board);
}

static void test_repeatable_loop_is_structurally_safe(void) {
    Board* board = BoardCreate(3, 2, "standard", 0);
    Coord me_body[] = {{0, 0}, {0, 1}, {1, 1}, {1, 0}};
    Coord you_body[] = {{2, 1}};
    Snake me = make_snake("me", me_body, 4, 90);
    Snake you = make_snake("you", you_body, 1, 90);
    CoreSearchConfig config = CoreSearchConfigDefault(1000);
    CoreSearchStats stats;
    MoveDirection move = MOVE_INVALID;

    config.fixed_depth = 1;
    assert(BoardAddSnake(board, &me));
    assert(BoardAddSnake(board, &you));
    assert(CoreMinimaxMoveWithStats(board, "me", config, &move, &stats) == CORE_OK);
    assert(stats.root_candidates[MOVE_RIGHT].structural_proof == CORE_STRUCTURAL_PROOF_SAFE);
    assert(stats.root_candidates[MOVE_RIGHT].proof_cutoff == CORE_STRUCTURAL_CUTOFF_CYCLE);
    assert(stats.root_candidates[MOVE_RIGHT].explored_states > 1);

    SnakeFree(&me);
    SnakeFree(&you);
    BoardFree(board);
}

static void test_low_health_loop_is_not_a_safe_structural_witness(void) {
    Board* board = BoardCreate(3, 2, "standard", 0);
    Coord me_body[] = {{0, 0}, {0, 1}, {1, 1}, {1, 0}};
    Coord you_body[] = {{2, 1}};
    Snake me = make_snake("me", me_body, 4, 2);
    Snake you = make_snake("you", you_body, 1, 90);
    CoreSearchConfig config = CoreSearchConfigDefault(1000);
    CoreSearchStats stats;
    MoveDirection move = MOVE_INVALID;

    config.fixed_depth = 1;
    assert(BoardAddSnake(board, &me));
    assert(BoardAddSnake(board, &you));
    assert(CoreMinimaxMoveWithStats(board, "me", config, &move, &stats) == CORE_OK);
    assert(stats.root_candidates[MOVE_RIGHT].structural_proof == CORE_STRUCTURAL_PROOF_UNKNOWN);
    assert(stats.root_candidates[MOVE_RIGHT].proof_cutoff == CORE_STRUCTURAL_CUTOFF_SURVIVABILITY);

    SnakeFree(&me);
    SnakeFree(&you);
    BoardFree(board);
}

static void test_root_hazard_damage_blocks_false_capacity_safety(void) {
    Board* board = BoardCreate(4, 3, "standard", 14);
    Coord me_body[] = {{1, 0}, {0, 0}, {0, 1}, {0, 2}};
    Coord you_body[] = {{3, 2}};
    Coord hazard = {2, 0};
    Snake me = make_snake("me", me_body, 4, 16);
    Snake you = make_snake("you", you_body, 1, 90);
    CoreSearchConfig config = CoreSearchConfigDefault(1000);
    CoreSearchStats stats;
    MoveDirection move = MOVE_INVALID;

    config.fixed_depth = 1;
    assert(BoardAddSnake(board, &me));
    assert(BoardAddSnake(board, &you));
    assert(BoardAddHazard(board, hazard));
    assert(CoreMinimaxMoveWithStats(board, "me", config, &move, &stats) == CORE_OK);
    assert(stats.root_candidates[MOVE_RIGHT].structural_proof == CORE_STRUCTURAL_PROOF_UNKNOWN);
    assert(stats.root_candidates[MOVE_RIGHT].proof_cutoff == CORE_STRUCTURAL_CUTOFF_SURVIVABILITY);

    SnakeFree(&me);
    SnakeFree(&you);
    BoardFree(board);
}

static void test_extended_horizon_proves_long_corridor_dead_end(void) {
    Board* board = BoardCreate(10, 1, "standard", 0);
    Coord me_body[] = {{2, 0}, {1, 0}, {0, 0}};
    Coord you_body[] = {{9, 0}};
    Snake me = make_snake("me", me_body, 3, 90);
    Snake you = make_snake("you", you_body, 1, 90);
    CoreSearchConfig config = CoreSearchConfigDefault(1000);
    CoreSearchStats stats;
    MoveDirection move = MOVE_INVALID;

    config.fixed_depth = 1;
    assert(BoardAddSnake(board, &me));
    assert(BoardAddSnake(board, &you));
    assert(CoreMinimaxMoveWithStats(board, "me", config, &move, &stats) == CORE_OK);
    assert(stats.root_candidates[MOVE_RIGHT].structural_proof == CORE_STRUCTURAL_PROOF_UNSAFE);
    assert(stats.root_candidates[MOVE_RIGHT].proof_cutoff == CORE_STRUCTURAL_CUTOFF_DEAD_END);

    SnakeFree(&me);
    SnakeFree(&you);
    BoardFree(board);
}

static void test_bounded_rectangle_cycle_proves_capacity(void) {
    Board* board = BoardCreate(4, 3, "standard", 0);
    Coord me_body[] = {{1, 0}, {0, 0}, {0, 1}, {0, 2}};
    Coord you_body[] = {{1, 1}};
    Snake me = make_snake("me", me_body, 4, 90);
    Snake you = make_snake("you", you_body, 1, 90);
    CoreSearchConfig config = CoreSearchConfigDefault(1000);
    CoreSearchStats stats;
    MoveDirection move = MOVE_INVALID;

    config.fixed_depth = 1;
    assert(BoardAddSnake(board, &me));
    assert(BoardAddSnake(board, &you));
    assert(CoreMinimaxMoveWithStats(board, "me", config, &move, &stats) == CORE_OK);
    assert(stats.root_candidates[MOVE_RIGHT].structural_proof == CORE_STRUCTURAL_PROOF_SAFE);
    assert(stats.root_candidates[MOVE_RIGHT].proof_cutoff == CORE_STRUCTURAL_CUTOFF_CAPACITY);
    assert(stats.root_candidates[MOVE_RIGHT].structural_capacity == 6);

    SnakeFree(&me);
    SnakeFree(&you);
    BoardFree(board);
}

int main(void) {
    test_root_comparison_orders_exact_outcomes_before_structure();
    test_root_comparison_uses_only_decisive_search_bounds();
    test_root_comparison_applies_structural_lattice_to_unresolved_values();
    test_root_comparison_prefers_later_exact_loss();
    test_root_comparison_uses_heuristic_only_after_structural_equality();
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
    test_branching_pocket_proves_every_branch_dies();
    test_repeatable_loop_is_structurally_safe();
    test_low_health_loop_is_not_a_safe_structural_witness();
    test_root_hazard_damage_blocks_false_capacity_safety();
    test_extended_horizon_proves_long_corridor_dead_end();
    test_bounded_rectangle_cycle_proves_capacity();
    return 0;
}
