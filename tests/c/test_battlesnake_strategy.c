#include "../../battlesnake/c-core/server/battlesnake_strategy.h"
#include "../../battlesnake/c-core/core/core_algorithms.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#ifdef CORE_ROOT_SELECTION_TESTING
extern bool CoreSelectRootCandidateForTesting(
    const Board* board,
    const char* snake_id,
    CoreRootPolicy policy,
    const MoveDirection* move_order,
    int move_count,
    uint8_t valid_mask,
    const CoreSearchValue values[4],
    const CoreRootCandidateStats candidates[4],
    MoveDirection preferred,
    MoveDirection* out_move,
    CoreSearchValue* out_value,
    CoreRootComparisonReason* out_reason
);
extern bool CoreRootTimeoutSnapshotForTesting(
    bool has_completed,
    MoveDirection completed_move,
    CoreSearchValue completed_value,
    CoreRootComparisonReason completed_reason,
    MoveDirection partial_move,
    CoreSearchValue partial_value,
    CoreRootComparisonReason partial_reason,
    MoveDirection* out_move,
    CoreSearchValue* out_value,
    CoreRootComparisonReason* out_reason,
    int* out_depth,
    CoreSelectionReason* out_selection_reason,
    bool out_root_value_valid[4],
    CoreSearchValue out_root_values[4]
);
#endif

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

#ifdef CORE_ROOT_SELECTION_TESTING
static void test_partial_root_frontier_is_permutation_invariant_and_coherent(void) {
    Board* board = BoardCreate(7, 7, "standard", 0);
    Coord me_body[] = {{3, 3}, {3, 2}, {3, 1}};
    Coord you_body[] = {{6, 6}, {6, 5}, {6, 4}};
    Snake me = make_snake("me", me_body, 3, 90);
    Snake you = make_snake("you", you_body, 3, 90);
    CoreSearchValue values[4];
    CoreRootCandidateStats candidates[4];
    memset(values, 0, sizeof(values));
    memset(candidates, 0, sizeof(candidates));
    for (int move = MOVE_UP; move <= MOVE_RIGHT; move++) {
        values[move] = root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_EXACT, 0.0, 0);
        candidates[move] = root_test_stats(CORE_STRUCTURAL_PROOF_NOT_ANALYZED, 0, 0);
    }
    values[MOVE_DOWN].score = 100.0;
    candidates[MOVE_DOWN] = root_test_stats(CORE_STRUCTURAL_PROOF_UNKNOWN, 1, 5);
    candidates[MOVE_LEFT] = root_test_stats(CORE_STRUCTURAL_PROOF_SAFE, 40, 5);
    const MoveDirection orders[][2] = {
        {MOVE_DOWN, MOVE_LEFT},
        {MOVE_LEFT, MOVE_DOWN},
    };

    assert(BoardAddSnake(board, &me));
    assert(BoardAddSnake(board, &you));
    CoreRootComparison pairwise = CoreCompareRootCandidates(
        &values[MOVE_LEFT],
        &candidates[MOVE_LEFT],
        &values[MOVE_DOWN],
        &candidates[MOVE_DOWN]
    );
    assert(pairwise.ordering == CORE_ROOT_COMPARISON_CANDIDATE);
    assert(pairwise.reason == CORE_ROOT_COMPARISON_STRUCTURAL_PROOF);
    MoveDirection selected = MOVE_INVALID;
    CoreSearchValue selected_value;
    CoreRootComparisonReason reason = CORE_ROOT_COMPARISON_NOT_COMPARED;
    for (size_t index = 0; index < sizeof(orders) / sizeof(orders[0]); index++) {
        selected = MOVE_INVALID;
        reason = CORE_ROOT_COMPARISON_NOT_COMPARED;
        assert(CoreSelectRootCandidateForTesting(
            board,
            "me",
            CORE_ROOT_POLICY_STANDARD_LADDER_OPPORTUNITY,
            orders[index],
            2,
            (uint8_t)((1u << MOVE_DOWN) | (1u << MOVE_LEFT)),
            values,
            candidates,
            MOVE_DOWN,
            &selected,
            &selected_value,
            &reason
        ));
        assert(selected == MOVE_LEFT);
        assert(selected_value.score == values[MOVE_LEFT].score);
        assert(selected_value.outcome == values[MOVE_LEFT].outcome);
        assert(selected_value.bound == values[MOVE_LEFT].bound);
        assert(selected_value.cause == values[MOVE_LEFT].cause);
        assert(selected_value.terminal_distance == values[MOVE_LEFT].terminal_distance);
        assert(reason == CORE_ROOT_COMPARISON_STRUCTURAL_PROOF);
    }

    values[MOVE_UP] = root_test_value(CORE_OUTCOME_LOSS, CORE_VALUE_BOUND_EXACT, -999000.0, 1);
    values[MOVE_RIGHT] = values[MOVE_UP];
    values[MOVE_DOWN] = root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_EXACT, 100.0, 0);
    values[MOVE_LEFT] = root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_EXACT, 0.0, 0);
    const MoveDirection all_moves[] = {MOVE_UP, MOVE_DOWN, MOVE_LEFT, MOVE_RIGHT};
    assert(CoreSelectRootCandidateForTesting(
        board,
        "me",
        CORE_ROOT_POLICY_STANDARD_LADDER_OPPORTUNITY,
        all_moves,
        4,
        0x0f,
        values,
        candidates,
        MOVE_DOWN,
        &selected,
        &selected_value,
        &reason
    ));
    assert(selected == MOVE_LEFT);
    assert(reason == CORE_ROOT_COMPARISON_STRUCTURAL_PROOF);

    values[MOVE_LEFT] = root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_UPPER, 0.0, 0);
    values[MOVE_DOWN] = root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_LOWER, 0.0, 0);
    pairwise = CoreCompareRootCandidates(
        &values[MOVE_LEFT],
        &candidates[MOVE_LEFT],
        &values[MOVE_DOWN],
        &candidates[MOVE_DOWN]
    );
    assert(pairwise.ordering == CORE_ROOT_COMPARISON_INCOMPARABLE);
    assert(pairwise.reason == CORE_ROOT_COMPARISON_SEARCH_BOUND);
    assert(CoreSelectRootCandidateForTesting(
        board,
        "me",
        CORE_ROOT_POLICY_STANDARD_LADDER_OPPORTUNITY,
        orders[1],
        2,
        (uint8_t)((1u << MOVE_DOWN) | (1u << MOVE_LEFT)),
        values,
        candidates,
        MOVE_DOWN,
        &selected,
        &selected_value,
        &reason
    ));
    assert(selected == MOVE_LEFT);
    assert(reason == CORE_ROOT_COMPARISON_STRUCTURAL_PROOF);

    /* T169 center-only completed tags: DOWN is exact unresolved while LEFT
     * is a fail-low unresolved bound. The pairwise search comparison remains
     * conservative, but the global structural frontier has an independent
     * SAFE proof over DOWN's capacity-deficient UNKNOWN. */
    candidates[MOVE_DOWN] = root_test_stats(CORE_STRUCTURAL_PROOF_UNKNOWN, 1, 12);
    candidates[MOVE_LEFT] = root_test_stats(CORE_STRUCTURAL_PROOF_SAFE, 109, 12);
    values[MOVE_DOWN] = root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_EXACT, 6.0, 0);
    values[MOVE_LEFT] = root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_UPPER, 6.0, 0);
    pairwise = CoreCompareRootCandidates(
        &values[MOVE_LEFT],
        &candidates[MOVE_LEFT],
        &values[MOVE_DOWN],
        &candidates[MOVE_DOWN]
    );
    assert(pairwise.ordering == CORE_ROOT_COMPARISON_INCOMPARABLE);
    assert(pairwise.reason == CORE_ROOT_COMPARISON_SEARCH_BOUND);
    assert(CoreSelectRootCandidateForTesting(
        board,
        "me",
        CORE_ROOT_POLICY_STANDARD_LADDER_OPPORTUNITY,
        orders[0],
        2,
        (uint8_t)((1u << MOVE_DOWN) | (1u << MOVE_LEFT)),
        values,
        candidates,
        MOVE_DOWN,
        &selected,
        &selected_value,
        &reason
    ));
    assert(selected == MOVE_LEFT);
    assert(reason == CORE_ROOT_COMPARISON_STRUCTURAL_PROOF);

    /* A touching exact/upper loss frontier proves neither root superior. The
     * SAFE certificate must therefore precede PV/geometry fallback even when
     * the incomplete search tags both roots as losses. */
    values[MOVE_LEFT] = root_test_value(CORE_OUTCOME_LOSS, CORE_VALUE_BOUND_EXACT, -991000.0, 9);
    values[MOVE_DOWN] = root_test_value(CORE_OUTCOME_LOSS, CORE_VALUE_BOUND_UPPER, -991000.0, 9);
    candidates[MOVE_LEFT] = root_test_stats(CORE_STRUCTURAL_PROOF_UNKNOWN, 4, 5);
    candidates[MOVE_DOWN] = root_test_stats(CORE_STRUCTURAL_PROOF_SAFE, 40, 5);
    assert(CoreSelectRootCandidateForTesting(
        board,
        "me",
        CORE_ROOT_POLICY_STANDARD_LADDER_OPPORTUNITY,
        orders[0],
        2,
        (uint8_t)((1u << MOVE_DOWN) | (1u << MOVE_LEFT)),
        values,
        candidates,
        MOVE_DOWN,
        &selected,
        &selected_value,
        &reason
    ));
    assert(selected == MOVE_DOWN);
    assert(reason == CORE_ROOT_COMPARISON_STRUCTURAL_PROOF);

    selected = MOVE_INVALID;
    reason = CORE_ROOT_COMPARISON_STABLE_DIRECTION;
    assert(CoreSelectRootCandidateForTesting(
        board,
        "me",
        CORE_ROOT_POLICY_STANDARD_LADDER_OPPORTUNITY,
        orders[0],
        2,
        (uint8_t)(1u << MOVE_DOWN),
        values,
        candidates,
        MOVE_LEFT,
        &selected,
        &selected_value,
        &reason
    ));
    assert(selected == MOVE_DOWN);
    assert(reason == CORE_ROOT_COMPARISON_NOT_COMPARED);

    values[MOVE_LEFT] = root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_EXACT, 0.0, 0);
    values[MOVE_DOWN] = root_test_value(CORE_OUTCOME_WIN, CORE_VALUE_BOUND_EXACT, 999000.0, 1);
    assert(CoreSelectRootCandidateForTesting(
        board,
        "me",
        CORE_ROOT_POLICY_STANDARD_LADDER_OPPORTUNITY,
        orders[1],
        2,
        (uint8_t)((1u << MOVE_DOWN) | (1u << MOVE_LEFT)),
        values,
        candidates,
        MOVE_LEFT,
        &selected,
        &selected_value,
        &reason
    ));
    assert(selected == MOVE_DOWN);
    assert(selected_value.outcome == CORE_OUTCOME_WIN);
    assert(reason == CORE_ROOT_COMPARISON_TERMINAL_OUTCOME);

    SnakeFree(&me);
    SnakeFree(&you);
    BoardFree(board);

    board = BoardCreate(6, 6, "standard", 0);
    Coord tie_me_body[] = {{1, 5}, {1, 4}, {2, 4}, {3, 4}, {3, 3}};
    Coord tie_you_body[] = {{3, 5}, {2, 5}};
    me = make_snake("me", tie_me_body, 5, 90);
    you = make_snake("you", tie_you_body, 2, 90);
    assert(BoardAddSnake(board, &me));
    assert(BoardAddSnake(board, &you));
    values[MOVE_LEFT] = root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_EXACT, 1.0, 0);
    values[MOVE_RIGHT] = values[MOVE_LEFT];
    values[MOVE_DOWN] = root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_EXACT, 0.0, 0);
    candidates[MOVE_LEFT] = root_test_stats(CORE_STRUCTURAL_PROOF_SAFE, 30, 5);
    candidates[MOVE_RIGHT] = candidates[MOVE_LEFT];
    pairwise = CoreCompareRootCandidates(
        &values[MOVE_LEFT],
        &candidates[MOVE_LEFT],
        &values[MOVE_RIGHT],
        &candidates[MOVE_RIGHT]
    );
    assert(pairwise.ordering == CORE_ROOT_COMPARISON_EQUAL);
    assert(pairwise.reason == CORE_ROOT_COMPARISON_NOT_COMPARED);

    const MoveDirection tie_orders[][3] = {
        {MOVE_DOWN, MOVE_LEFT, MOVE_RIGHT},
        {MOVE_RIGHT, MOVE_DOWN, MOVE_LEFT},
    };
    for (size_t index = 0; index < sizeof(tie_orders) / sizeof(tie_orders[0]); index++) {
        assert(CoreSelectRootCandidateForTesting(
            board,
            "me",
            CORE_ROOT_POLICY_STRICT_MINIMAX,
            tie_orders[index],
            3,
            (uint8_t)((1u << MOVE_DOWN) | (1u << MOVE_LEFT) | (1u << MOVE_RIGHT)),
            values,
            candidates,
            MOVE_RIGHT,
            &selected,
            &selected_value,
            &reason
        ));
        assert(selected == MOVE_LEFT);
        assert(selected_value.score == 1.0);
        assert(selected_value.outcome == CORE_OUTCOME_UNRESOLVED);
        assert(selected_value.bound == CORE_VALUE_BOUND_EXACT);
        assert(reason == CORE_ROOT_COMPARISON_STRUCTURAL_TIEBREAK);
    }

    SnakeFree(&me);
    SnakeFree(&you);
    BoardFree(board);

    board = BoardCreate(7, 7, "standard", 0);
    Coord layered_me_body[] = {
        {4, 3}, {3, 3}, {3, 4}, {3, 5}, {3, 6},
        {4, 6}, {5, 6}, {5, 5}, {5, 4}, {5, 3},
    };
    Coord layered_you_body[] = {{4, 0}, {4, 1}};
    me = make_snake("me", layered_me_body, 10, 90);
    you = make_snake("you", layered_you_body, 2, 90);
    assert(BoardAddSnake(board, &me));
    assert(BoardAddSnake(board, &you));
    for (int move = MOVE_UP; move <= MOVE_RIGHT; move++) {
        values[move] = root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_EXACT, 1.0, 0);
        candidates[move] = root_test_stats(CORE_STRUCTURAL_PROOF_SAFE, 40, 10);
    }
    const MoveDirection layered_moves[] = {MOVE_UP, MOVE_DOWN, MOVE_RIGHT};
    uint8_t layered_mask = (uint8_t)((1u << MOVE_UP) | (1u << MOVE_DOWN) | (1u << MOVE_RIGHT));
    assert(CoreSelectRootCandidateForTesting(
        board,
        "me",
        CORE_ROOT_POLICY_STRICT_MINIMAX,
        layered_moves,
        3,
        layered_mask,
        values,
        candidates,
        MOVE_RIGHT,
        &selected,
        &selected_value,
        &reason
    ));
    assert(selected == MOVE_RIGHT);
    assert(reason == CORE_ROOT_COMPARISON_PREVIOUS_PV);
    assert(CoreSelectRootCandidateForTesting(
        board,
        "me",
        CORE_ROOT_POLICY_STRICT_MINIMAX,
        layered_moves,
        3,
        layered_mask,
        values,
        candidates,
        MOVE_INVALID,
        &selected,
        &selected_value,
        &reason
    ));
    assert(selected == MOVE_DOWN);
    assert(reason == CORE_ROOT_COMPARISON_STABLE_DIRECTION);

    values[MOVE_DOWN] = root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_EXACT, 10.0, 0);
    values[MOVE_UP] = root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_EXACT, 5.0, 0);
    values[MOVE_RIGHT] = root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_UPPER, 0.0, 0);
    const MoveDirection mixed_numeric_orders[][3] = {
        {MOVE_UP, MOVE_DOWN, MOVE_RIGHT},
        {MOVE_RIGHT, MOVE_DOWN, MOVE_UP},
    };
    for (size_t index = 0;
         index < sizeof(mixed_numeric_orders) / sizeof(mixed_numeric_orders[0]);
         index++) {
        assert(CoreSelectRootCandidateForTesting(
            board,
            "me",
            CORE_ROOT_POLICY_STANDARD_LADDER_OPPORTUNITY,
            mixed_numeric_orders[index],
            3,
            layered_mask,
            values,
            candidates,
            MOVE_INVALID,
            &selected,
            &selected_value,
            &reason
        ));
        assert(selected == MOVE_DOWN);
        assert(selected_value.score == 10.0);
        assert(selected_value.bound == CORE_VALUE_BOUND_EXACT);
        assert(reason == CORE_ROOT_COMPARISON_SEARCH_BOUND);
    }

    values[MOVE_DOWN] = root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_EXACT, 10.0, 0);
    values[MOVE_RIGHT] = root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_UPPER, 0.0, 0);
    assert(CoreSelectRootCandidateForTesting(
        board,
        "me",
        CORE_ROOT_POLICY_STRICT_MINIMAX,
        layered_moves + 1,
        2,
        (uint8_t)((1u << MOVE_DOWN) | (1u << MOVE_RIGHT)),
        values,
        candidates,
        MOVE_INVALID,
        &selected,
        &selected_value,
        &reason
    ));
    assert(selected == MOVE_DOWN);
    assert(reason == CORE_ROOT_COMPARISON_SEARCH_BOUND);

    values[MOVE_DOWN] = root_test_value(CORE_OUTCOME_WIN, CORE_VALUE_BOUND_EXACT, 999000.0, 1);
    values[MOVE_RIGHT] = root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_UPPER, 0.0, 0);
    assert(CoreSelectRootCandidateForTesting(
        board,
        "me",
        CORE_ROOT_POLICY_STRICT_MINIMAX,
        layered_moves + 1,
        2,
        (uint8_t)((1u << MOVE_DOWN) | (1u << MOVE_RIGHT)),
        values,
        candidates,
        MOVE_INVALID,
        &selected,
        &selected_value,
        &reason
    ));
    assert(selected == MOVE_DOWN);
    assert(reason == CORE_ROOT_COMPARISON_SEARCH_BOUND);

    SnakeFree(&me);
    SnakeFree(&you);
    BoardFree(board);
}

static void test_exceptional_and_mixed_strict_root_values_are_truthful(void) {
    Board* board = BoardCreate(7, 7, "standard", 0);
    Coord me_body[] = {{3, 3}, {3, 2}, {3, 1}};
    Coord you_body[] = {{6, 6}, {6, 5}, {6, 4}};
    Snake me = make_snake("me", me_body, 3, 90);
    Snake you = make_snake("you", you_body, 3, 90);
    CoreSearchValue values[4];
    CoreRootCandidateStats candidates[4];
    memset(values, 0, sizeof(values));
    memset(candidates, 0, sizeof(candidates));
    for (int move = MOVE_UP; move <= MOVE_RIGHT; move++) {
        candidates[move] = root_test_stats(CORE_STRUCTURAL_PROOF_SAFE, 40, 3);
    }
    assert(BoardAddSnake(board, &me));
    assert(BoardAddSnake(board, &you));
    const MoveDirection forward[] = {MOVE_UP, MOVE_LEFT};
    const MoveDirection reverse[] = {MOVE_LEFT, MOVE_UP};
    const uint8_t mask = (uint8_t)((1u << MOVE_UP) | (1u << MOVE_LEFT));
    MoveDirection selected = MOVE_INVALID;
    CoreSearchValue selected_value;
    CoreRootComparisonReason reason = CORE_ROOT_COMPARISON_NOT_COMPARED;

    values[MOVE_UP] = root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_EXACT, 1.0, 0);
    values[MOVE_LEFT] = root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_EXACT, INFINITY, 0);
    CoreRootComparison typed = CoreCompareRootCandidates(
        &values[MOVE_UP],
        &candidates[MOVE_UP],
        &values[MOVE_LEFT],
        &candidates[MOVE_LEFT]
    );
    assert(typed.ordering == CORE_ROOT_COMPARISON_CANDIDATE);
    assert(CoreSelectRootCandidateForTesting(
        board,
        "me",
        CORE_ROOT_POLICY_STANDARD_LADDER_OPPORTUNITY,
        forward,
        2,
        mask,
        values,
        candidates,
        MOVE_INVALID,
        &selected,
        &selected_value,
        &reason
    ));
    assert(selected == MOVE_UP);
    assert(selected_value.score == 1.0);
    assert(reason == CORE_ROOT_COMPARISON_HEURISTIC_VALUE);

    const MoveDirection* orders[] = {forward, reverse};
    const MoveDirection expected[] = {MOVE_UP, MOVE_LEFT};

    values[MOVE_UP] = root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_EXACT, INFINITY, 0);
    values[MOVE_LEFT] = root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_EXACT, 2.0, 0);
    assert(CoreSelectRootCandidateForTesting(
        board, "me", CORE_ROOT_POLICY_STRICT_MINIMAX, reverse, 2, mask,
        values, candidates, MOVE_INVALID, &selected, &selected_value, &reason
    ));
    assert(selected == MOVE_UP);
    assert(isinf(selected_value.score) && selected_value.score > 0.0);
    assert(reason == CORE_ROOT_COMPARISON_HEURISTIC_VALUE);

    for (size_t index = 0; index < 2; index++) {
        values[MOVE_UP] = root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_EXACT, -INFINITY, 0);
        values[MOVE_LEFT] = values[MOVE_UP];
        assert(CoreSelectRootCandidateForTesting(
            board, "me", CORE_ROOT_POLICY_STRICT_MINIMAX, orders[index], 2, mask,
            values, candidates, MOVE_INVALID, &selected, &selected_value, &reason
        ));
        assert(selected == expected[index]);
        assert(isinf(selected_value.score) && selected_value.score < 0.0);
        assert(reason == CORE_ROOT_COMPARISON_STABLE_DIRECTION);

        values[MOVE_UP].score = NAN;
        values[MOVE_LEFT].score = NAN;
        assert(CoreSelectRootCandidateForTesting(
            board, "me", CORE_ROOT_POLICY_STRICT_MINIMAX, orders[index], 2, mask,
            values, candidates, MOVE_INVALID, &selected, &selected_value, &reason
        ));
        assert(selected == expected[index]);
        assert(isnan(selected_value.score));
        assert(reason == CORE_ROOT_COMPARISON_STABLE_DIRECTION);
    }

    for (size_t index = 0; index < 2; index++) {
        values[MOVE_UP] = root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_EXACT, 2.0, 0);
        values[MOVE_LEFT] = root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_EXACT, NAN, 0);
        assert(CoreSelectRootCandidateForTesting(
            board, "me", CORE_ROOT_POLICY_STRICT_MINIMAX, orders[index], 2, mask,
            values, candidates, MOVE_INVALID, &selected, &selected_value, &reason
        ));
        assert(selected == MOVE_UP);
        assert(selected_value.score == 2.0);
        assert(reason == CORE_ROOT_COMPARISON_HEURISTIC_VALUE);
    }

    values[MOVE_UP] = root_test_value(CORE_OUTCOME_LOSS, CORE_VALUE_BOUND_EXACT, 10.0, 2);
    values[MOVE_LEFT] = root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_EXACT, 0.0, 0);
    assert(CoreSelectRootCandidateForTesting(
        board, "me", CORE_ROOT_POLICY_STRICT_MINIMAX, forward, 2, mask,
        values, candidates, MOVE_INVALID, &selected, &selected_value, &reason
    ));
    assert(selected == MOVE_UP);
    assert(reason == CORE_ROOT_COMPARISON_NUMERIC_VALUE);

    values[MOVE_UP] = root_test_value(CORE_OUTCOME_WIN, CORE_VALUE_BOUND_EXACT, 10.0, 1);
    values[MOVE_LEFT] = root_test_value(CORE_OUTCOME_LOSS, CORE_VALUE_BOUND_EXACT, 0.0, 5);
    assert(CoreSelectRootCandidateForTesting(
        board, "me", CORE_ROOT_POLICY_STRICT_MINIMAX, forward, 2, mask,
        values, candidates, MOVE_INVALID, &selected, &selected_value, &reason
    ));
    assert(selected == MOVE_UP);
    assert(reason == CORE_ROOT_COMPARISON_TERMINAL_OUTCOME);

    values[MOVE_UP] = root_test_value(CORE_OUTCOME_DRAW, CORE_VALUE_BOUND_EXACT, 10.0, 0);
    values[MOVE_LEFT] = root_test_value(CORE_OUTCOME_WIN, CORE_VALUE_BOUND_EXACT, 0.0, 1);
    assert(CoreSelectRootCandidateForTesting(
        board, "me", CORE_ROOT_POLICY_STRICT_MINIMAX, forward, 2, mask,
        values, candidates, MOVE_INVALID, &selected, &selected_value, &reason
    ));
    assert(selected == MOVE_UP);
    assert(reason == CORE_ROOT_COMPARISON_NUMERIC_VALUE);

    values[MOVE_UP] = root_test_value(CORE_OUTCOME_LOSS, CORE_VALUE_BOUND_EXACT, 10.0, 5);
    values[MOVE_LEFT] = root_test_value(CORE_OUTCOME_LOSS, CORE_VALUE_BOUND_EXACT, 0.0, 3);
    assert(CoreSelectRootCandidateForTesting(
        board, "me", CORE_ROOT_POLICY_STRICT_MINIMAX, forward, 2, mask,
        values, candidates, MOVE_INVALID, &selected, &selected_value, &reason
    ));
    assert(selected == MOVE_UP);
    assert(reason == CORE_ROOT_COMPARISON_TERMINAL_SURVIVAL);

    values[MOVE_UP].terminal_distance = 2;
    values[MOVE_LEFT].terminal_distance = 5;
    assert(CoreSelectRootCandidateForTesting(
        board, "me", CORE_ROOT_POLICY_STRICT_MINIMAX, forward, 2, mask,
        values, candidates, MOVE_INVALID, &selected, &selected_value, &reason
    ));
    assert(selected == MOVE_UP);
    assert(reason == CORE_ROOT_COMPARISON_NUMERIC_VALUE);

    SnakeFree(&me);
    SnakeFree(&you);
    BoardFree(board);
}

static void assert_nonfinite_frontier_uses_deterministic_fallback(
    Board* board,
    const CoreSearchValue values[4],
    const CoreRootCandidateStats candidates[4],
    const MoveDirection orders[][2],
    uint8_t mask
) {
    CoreRootComparison typed = CoreCompareRootCandidates(
        &values[MOVE_UP],
        &candidates[MOVE_UP],
        &values[MOVE_LEFT],
        &candidates[MOVE_LEFT]
    );
    CoreRootComparison reverse = CoreCompareRootCandidates(
        &values[MOVE_LEFT],
        &candidates[MOVE_LEFT],
        &values[MOVE_UP],
        &candidates[MOVE_UP]
    );
    bool identical_tagged_infinity =
        values[MOVE_UP].bound == values[MOVE_LEFT].bound &&
        isinf(values[MOVE_UP].score) &&
        values[MOVE_UP].score == values[MOVE_LEFT].score;
    CoreRootComparisonOrdering expected_ordering = identical_tagged_infinity ?
        CORE_ROOT_COMPARISON_EQUAL : CORE_ROOT_COMPARISON_INCOMPARABLE;
    assert(typed.ordering == expected_ordering);
    assert(reverse.ordering == expected_ordering);
    for (size_t order_index = 0; order_index < 2; order_index++) {
        MoveDirection expected_move = MOVE_INVALID;
        CoreRootComparisonReason expected_reason = CORE_ROOT_COMPARISON_NOT_COMPARED;
        for (int repeat = 0; repeat < 2; repeat++) {
            MoveDirection selected = MOVE_INVALID;
            CoreSearchValue selected_value;
            CoreRootComparisonReason reason = CORE_ROOT_COMPARISON_NOT_COMPARED;
            assert(CoreSelectRootCandidateForTesting(
                board,
                "me",
                CORE_ROOT_POLICY_STANDARD_LADDER_OPPORTUNITY,
                orders[order_index],
                2,
                mask,
                values,
                candidates,
                MOVE_INVALID,
                &selected,
                &selected_value,
                &reason
            ));
            assert(selected == MOVE_UP || selected == MOVE_LEFT);
            assert(reason != CORE_ROOT_COMPARISON_SEARCH_BOUND);
            assert(reason != CORE_ROOT_COMPARISON_HEURISTIC_VALUE);
            assert(reason != CORE_ROOT_COMPARISON_NUMERIC_VALUE);
            if (repeat == 0) {
                expected_move = selected;
                expected_reason = reason;
            } else {
                assert(selected == expected_move);
                assert(reason == expected_reason);
            }
        }
    }
}

static void test_nonfinite_bound_frontiers_do_not_claim_numeric_dominance(void) {
    Board* board = BoardCreate(7, 7, "standard", 0);
    Coord me_body[] = {{3, 3}, {3, 2}, {3, 1}};
    Coord you_body[] = {{6, 6}, {6, 5}, {6, 4}};
    Snake me = make_snake("me", me_body, 3, 90);
    Snake you = make_snake("you", you_body, 3, 90);
    CoreSearchValue values[4];
    CoreRootCandidateStats candidates[4];
    const CoreValueBound bounds[] = {CORE_VALUE_BOUND_LOWER, CORE_VALUE_BOUND_UPPER};
    const double exceptional[] = {NAN, INFINITY, -INFINITY};
    const MoveDirection root_moves[] = {MOVE_UP, MOVE_LEFT};
    const MoveDirection orders[][2] = {{MOVE_UP, MOVE_LEFT}, {MOVE_LEFT, MOVE_UP}};
    const uint8_t mask = (uint8_t)((1u << MOVE_UP) | (1u << MOVE_LEFT));

    memset(values, 0, sizeof(values));
    memset(candidates, 0, sizeof(candidates));
    for (int move = MOVE_UP; move <= MOVE_RIGHT; move++) {
        candidates[move] = root_test_stats(CORE_STRUCTURAL_PROOF_SAFE, 40, 3);
    }
    assert(BoardAddSnake(board, &me));
    assert(BoardAddSnake(board, &you));

    for (size_t bound_index = 0;
         bound_index < sizeof(bounds) / sizeof(bounds[0]);
         bound_index++) {
        for (size_t value_index = 0;
             value_index < sizeof(exceptional) / sizeof(exceptional[0]);
             value_index++) {
            for (size_t role_index = 0;
                 role_index < sizeof(root_moves) / sizeof(root_moves[0]);
                 role_index++) {
                MoveDirection exceptional_move = root_moves[role_index];
                MoveDirection finite_move = exceptional_move == MOVE_UP ? MOVE_LEFT : MOVE_UP;
                values[finite_move] = root_test_value(
                    CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_EXACT, 1.0, 0
                );
                values[exceptional_move] = root_test_value(
                    CORE_OUTCOME_UNRESOLVED,
                    bounds[bound_index],
                    exceptional[value_index],
                    0
                );
                assert_nonfinite_frontier_uses_deterministic_fallback(
                    board, values, candidates, orders, mask
                );
            }
        }
    }

    for (size_t up_bound = 0;
         up_bound < sizeof(bounds) / sizeof(bounds[0]);
         up_bound++) {
        for (size_t left_bound = 0;
             left_bound < sizeof(bounds) / sizeof(bounds[0]);
             left_bound++) {
            for (size_t up_value = 0;
                 up_value < sizeof(exceptional) / sizeof(exceptional[0]);
                 up_value++) {
                for (size_t left_value = 0;
                     left_value < sizeof(exceptional) / sizeof(exceptional[0]);
                     left_value++) {
                    values[MOVE_UP] = root_test_value(
                        CORE_OUTCOME_UNRESOLVED,
                        bounds[up_bound],
                        exceptional[up_value],
                        0
                    );
                    values[MOVE_LEFT] = root_test_value(
                        CORE_OUTCOME_UNRESOLVED,
                        bounds[left_bound],
                        exceptional[left_value],
                        0
                    );
                    assert_nonfinite_frontier_uses_deterministic_fallback(
                        board, values, candidates, orders, mask
                    );
                }
            }
        }
    }

    SnakeFree(&me);
    SnakeFree(&you);
    BoardFree(board);
}

static void test_mixed_loss_frontier_keeps_exact_terminal_survival_pairwise(void) {
    Board* board = BoardCreate(7, 7, "standard", 0);
    Coord me_body[] = {{3, 3}, {3, 2}, {3, 1}};
    Coord you_body[] = {{6, 6}, {6, 5}, {6, 4}};
    Snake me = make_snake("me", me_body, 3, 90);
    Snake you = make_snake("you", you_body, 3, 90);
    CoreSearchValue values[4];
    CoreRootCandidateStats candidates[4];
    const MoveDirection orders[][3] = {
        {MOVE_UP, MOVE_LEFT, MOVE_RIGHT},
        {MOVE_UP, MOVE_RIGHT, MOVE_LEFT},
        {MOVE_LEFT, MOVE_UP, MOVE_RIGHT},
        {MOVE_LEFT, MOVE_RIGHT, MOVE_UP},
        {MOVE_RIGHT, MOVE_UP, MOVE_LEFT},
        {MOVE_RIGHT, MOVE_LEFT, MOVE_UP},
    };
    const uint8_t mask = (uint8_t)(
        (1u << MOVE_UP) | (1u << MOVE_LEFT) | (1u << MOVE_RIGHT)
    );
    bool selected_exact = false;
    bool selected_bounded = false;

    memset(values, 0, sizeof(values));
    memset(candidates, 0, sizeof(candidates));
    for (int move = MOVE_UP; move <= MOVE_RIGHT; move++) {
        candidates[move] = root_test_stats(CORE_STRUCTURAL_PROOF_SAFE, 40, 3);
    }
    values[MOVE_UP] = root_test_value(
        CORE_OUTCOME_LOSS, CORE_VALUE_BOUND_EXACT, 0.0, 10
    );
    values[MOVE_LEFT] = root_test_value(
        CORE_OUTCOME_LOSS, CORE_VALUE_BOUND_EXACT, 100.0, 5
    );
    values[MOVE_RIGHT] = root_test_value(
        CORE_OUTCOME_LOSS, CORE_VALUE_BOUND_UPPER, 50.0, 0
    );
    assert(BoardAddSnake(board, &me));
    assert(BoardAddSnake(board, &you));

    for (size_t index = 0; index < sizeof(orders) / sizeof(orders[0]); index++) {
        MoveDirection selected = MOVE_INVALID;
        CoreSearchValue selected_value;
        CoreRootComparisonReason reason = CORE_ROOT_COMPARISON_NOT_COMPARED;
        assert(CoreSelectRootCandidateForTesting(
            board,
            "me",
            CORE_ROOT_POLICY_STANDARD_LADDER_OPPORTUNITY,
            orders[index],
            3,
            mask,
            values,
            candidates,
            MOVE_INVALID,
            &selected,
            &selected_value,
            &reason
        ));
        assert(selected != MOVE_LEFT);
        selected_exact = selected_exact || selected == MOVE_UP;
        selected_bounded = selected_bounded || selected == MOVE_RIGHT;
    }
    assert(selected_exact);
    assert(selected_bounded);

    SnakeFree(&me);
    SnakeFree(&you);
    BoardFree(board);
}

static void test_timeout_snapshot_preserves_complete_or_adopts_coherent_partial(void) {
    CoreSearchValue completed = root_test_value(
        CORE_OUTCOME_UNRESOLVED,
        CORE_VALUE_BOUND_EXACT,
        5.0,
        0
    );
    CoreSearchValue partial = root_test_value(
        CORE_OUTCOME_WIN,
        CORE_VALUE_BOUND_UPPER,
        10.0,
        3
    );
    partial.cause = CORE_TERMINAL_CAUSE_OPPONENT_ELIMINATED;
    MoveDirection move = MOVE_INVALID;
    CoreSearchValue value;
    CoreRootComparisonReason reason = CORE_ROOT_COMPARISON_NOT_COMPARED;
    int depth = -1;
    CoreSelectionReason selection_reason = CORE_SELECTION_CORRIDOR_GUARD;
    bool root_value_valid[4] = {false, false, false, false};
    CoreSearchValue root_values[4];
    memset(root_values, 0, sizeof(root_values));

    for (int repetition = 0; repetition < 3; repetition++) {
        assert(CoreRootTimeoutSnapshotForTesting(
            true,
            MOVE_LEFT,
            completed,
            CORE_ROOT_COMPARISON_HEURISTIC_VALUE,
            MOVE_RIGHT,
            partial,
            CORE_ROOT_COMPARISON_STRUCTURAL_PROOF,
            &move,
            &value,
            &reason,
            &depth,
            &selection_reason,
            root_value_valid,
            root_values
        ));
        assert(depth == 1);
        assert(selection_reason == CORE_SELECTION_MINIMAX);
        assert(move == MOVE_LEFT);
        assert(value.score == completed.score);
        assert(value.outcome == completed.outcome);
        assert(value.bound == completed.bound);
        assert(reason == CORE_ROOT_COMPARISON_HEURISTIC_VALUE);
        assert(root_value_valid[MOVE_LEFT]);
        assert(!root_value_valid[MOVE_RIGHT]);
        assert(root_values[MOVE_LEFT].score == completed.score);
        assert(root_values[MOVE_LEFT].bound == completed.bound);
    }

    assert(CoreRootTimeoutSnapshotForTesting(
        false,
        MOVE_INVALID,
        completed,
        CORE_ROOT_COMPARISON_NOT_COMPARED,
        MOVE_RIGHT,
        partial,
        CORE_ROOT_COMPARISON_STRUCTURAL_PROOF,
        &move,
        &value,
        &reason,
        &depth,
        &selection_reason,
        root_value_valid,
        root_values
    ));
    assert(depth == 0);
    assert(selection_reason == CORE_SELECTION_TIMEOUT_BEST_SO_FAR);
    assert(move == MOVE_RIGHT);
    assert(value.score == partial.score);
    assert(value.outcome == partial.outcome);
    assert(value.bound == partial.bound);
    assert(value.cause == partial.cause);
    assert(value.terminal_distance == partial.terminal_distance);
    assert(reason == CORE_ROOT_COMPARISON_STRUCTURAL_PROOF);
    assert(!root_value_valid[MOVE_LEFT]);
    assert(root_value_valid[MOVE_RIGHT]);
    assert(root_values[MOVE_RIGHT].score == partial.score);
    assert(root_values[MOVE_RIGHT].bound == partial.bound);

    assert(!CoreRootTimeoutSnapshotForTesting(
        false,
        MOVE_INVALID,
        completed,
        CORE_ROOT_COMPARISON_HEURISTIC_VALUE,
        MOVE_INVALID,
        partial,
        CORE_ROOT_COMPARISON_STRUCTURAL_PROOF,
        &move,
        &value,
        &reason,
        &depth,
        &selection_reason,
        root_value_valid,
        root_values
    ));
    assert(depth == 0);
    assert(selection_reason == CORE_SELECTION_ALLOWED_FALLBACK);
    assert(move == MOVE_INVALID);
    assert(reason == CORE_ROOT_COMPARISON_NOT_COMPARED);
    for (int root_move = MOVE_UP; root_move <= MOVE_RIGHT; root_move++) {
        assert(!root_value_valid[root_move]);
    }
}
#endif

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
        CORE_ROOT_COMPARISON_INCOMPARABLE,
        CORE_ROOT_COMPARISON_SEARCH_BOUND
    );
    assert_root_comparison(
        root_test_value(CORE_OUTCOME_DRAW, CORE_VALUE_BOUND_UPPER, 0.0, 0),
        equal_structure,
        root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_LOWER, 0.0, 0),
        equal_structure,
        CORE_ROOT_COMPARISON_INCOMPARABLE,
        CORE_ROOT_COMPARISON_SEARCH_BOUND
    );
    assert_root_comparison(
        root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_EXACT, 0.0, 0),
        equal_structure,
        root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_UPPER, 0.0, 0),
        equal_structure,
        CORE_ROOT_COMPARISON_INCOMPARABLE,
        CORE_ROOT_COMPARISON_SEARCH_BOUND
    );
    assert_root_comparison(
        root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_UPPER, 0.0, 0),
        equal_structure,
        root_test_value(CORE_OUTCOME_LOSS, CORE_VALUE_BOUND_EXACT, 0.0, 0),
        equal_structure,
        CORE_ROOT_COMPARISON_INCOMPARABLE,
        CORE_ROOT_COMPARISON_SEARCH_BOUND
    );
    assert_root_comparison(
        root_test_value(CORE_OUTCOME_DRAW, CORE_VALUE_BOUND_LOWER, -100.0, 7),
        equal_structure,
        root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_UPPER, 100.0, 19),
        equal_structure,
        CORE_ROOT_COMPARISON_CANDIDATE,
        CORE_ROOT_COMPARISON_SEARCH_BOUND
    );
}

static void test_root_comparison_matches_expected_interval_table(void) {
    typedef struct {
        CoreSearchValue candidate;
        CoreSearchValue incumbent;
        CoreRootComparisonOrdering ordering;
        CoreRootComparisonReason reason;
    } ExpectedIntervalCase;
    CoreRootCandidateStats stats = root_test_stats(CORE_STRUCTURAL_PROOF_SAFE, 8, 3);
    ExpectedIntervalCase cases[] = {
        {
            root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_EXACT, 500.0, 9),
            root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_UPPER, -500.0, 1),
            CORE_ROOT_COMPARISON_INCOMPARABLE,
            CORE_ROOT_COMPARISON_SEARCH_BOUND,
        },
        {
            root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_UPPER, 70.0, 6),
            root_test_value(CORE_OUTCOME_LOSS, CORE_VALUE_BOUND_EXACT, -70.0, 2),
            CORE_ROOT_COMPARISON_INCOMPARABLE,
            CORE_ROOT_COMPARISON_SEARCH_BOUND,
        },
        {
            root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_UPPER, -3.0, 4),
            root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_LOWER, 3.0, 12),
            CORE_ROOT_COMPARISON_INCOMPARABLE,
            CORE_ROOT_COMPARISON_SEARCH_BOUND,
        },
        {
            root_test_value(CORE_OUTCOME_DRAW, CORE_VALUE_BOUND_LOWER, -900.0, 13),
            root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_UPPER, 900.0, 2),
            CORE_ROOT_COMPARISON_CANDIDATE,
            CORE_ROOT_COMPARISON_SEARCH_BOUND,
        },
        {
            root_test_value(CORE_OUTCOME_DRAW, CORE_VALUE_BOUND_EXACT, -1.0, 8),
            root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_EXACT, 1.0, 3),
            CORE_ROOT_COMPARISON_CANDIDATE,
            CORE_ROOT_COMPARISON_TERMINAL_OUTCOME,
        },
    };

    for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
        assert_root_comparison(
            cases[index].candidate,
            stats,
            cases[index].incumbent,
            stats,
            cases[index].ordering,
            cases[index].reason
        );
    }
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
    CoreRootCandidateStats missing_capacity_unknown = root_test_stats(
        CORE_STRUCTURAL_PROOF_UNKNOWN,
        0,
        0
    );
    CoreRootCandidateStats invalid_capacity_unknown = root_test_stats(
        CORE_STRUCTURAL_PROOF_UNKNOWN,
        -1,
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
        CORE_ROOT_COMPARISON_INCOMPARABLE,
        CORE_ROOT_COMPARISON_NOT_COMPARED
    );
    assert_root_comparison(
        structurally_preferred_value,
        safe,
        heuristic_preferred_value,
        missing_capacity_unknown,
        CORE_ROOT_COMPARISON_CANDIDATE,
        CORE_ROOT_COMPARISON_STRUCTURAL_PROOF
    );
    assert_root_comparison(
        structurally_preferred_value,
        safe,
        heuristic_preferred_value,
        invalid_capacity_unknown,
        CORE_ROOT_COMPARISON_CANDIDATE,
        CORE_ROOT_COMPARISON_STRUCTURAL_PROOF
    );
}

static void test_root_comparison_structural_relation_is_not_a_total_rank(void) {
    const CoreRootCandidateStats structures[] = {
        {.structural_proof = CORE_STRUCTURAL_PROOF_SAFE, .relaxed_static_capacity = 8,
         .post_move_length = 4},
        {.structural_proof = CORE_STRUCTURAL_PROOF_UNSAFE, .relaxed_static_capacity = 20,
         .post_move_length = 4},
        {.structural_proof = CORE_STRUCTURAL_PROOF_UNKNOWN, .relaxed_static_capacity = 3,
         .post_move_length = 4},
        {.structural_proof = CORE_STRUCTURAL_PROOF_UNKNOWN, .relaxed_static_capacity = 4,
         .post_move_length = 4},
    };
    CoreSearchValue lower_heuristic = root_test_value(
        CORE_OUTCOME_UNRESOLVED,
        CORE_VALUE_BOUND_EXACT,
        -100.0,
        0
    );
    CoreSearchValue higher_heuristic = root_test_value(
        CORE_OUTCOME_UNRESOLVED,
        CORE_VALUE_BOUND_EXACT,
        100.0,
        0
    );

    for (size_t candidate = 0; candidate < 4; candidate++) {
        for (size_t incumbent = 0; incumbent < 4; incumbent++) {
            bool candidate_structurally_dominates = candidate == 0 &&
                (incumbent == 1 || incumbent == 2);
            bool incumbent_structurally_dominates = incumbent == 0 &&
                (candidate == 1 || candidate == 2);
            CoreRootComparison comparison = CoreCompareRootCandidates(
                &lower_heuristic,
                &structures[candidate],
                &higher_heuristic,
                &structures[incumbent]
            );
            if (candidate_structurally_dominates) {
                assert(comparison.ordering == CORE_ROOT_COMPARISON_CANDIDATE);
                assert(comparison.reason == CORE_ROOT_COMPARISON_STRUCTURAL_PROOF);
            } else if (incumbent_structurally_dominates) {
                assert(comparison.ordering == CORE_ROOT_COMPARISON_INCUMBENT);
                assert(comparison.reason == CORE_ROOT_COMPARISON_STRUCTURAL_PROOF);
            } else {
                assert(comparison.ordering == CORE_ROOT_COMPARISON_INCUMBENT);
                assert(comparison.reason == CORE_ROOT_COMPARISON_HEURISTIC_VALUE);
            }
        }
    }
}

static void test_root_comparison_requires_non_exact_semantic_identity(void) {
    CoreRootCandidateStats structure = root_test_stats(CORE_STRUCTURAL_PROOF_UNKNOWN, 5, 3);
    CoreSearchValue upper = root_test_value(
        CORE_OUTCOME_UNRESOLVED,
        CORE_VALUE_BOUND_UPPER,
        10.0,
        4
    );

    assert_root_comparison(
        upper,
        structure,
        upper,
        structure,
        CORE_ROOT_COMPARISON_EQUAL,
        CORE_ROOT_COMPARISON_NOT_COMPARED
    );
    CoreSearchValue different_score = upper;
    different_score.score = 11.0;
    assert_root_comparison(
        upper,
        structure,
        different_score,
        structure,
        CORE_ROOT_COMPARISON_INCOMPARABLE,
        CORE_ROOT_COMPARISON_NOT_COMPARED
    );
    CoreSearchValue different_distance = upper;
    different_distance.terminal_distance = 5;
    assert_root_comparison(
        upper,
        structure,
        different_distance,
        structure,
        CORE_ROOT_COMPARISON_INCOMPARABLE,
        CORE_ROOT_COMPARISON_NOT_COMPARED
    );
    CoreRootCandidateStats different_structure = structure;
    different_structure.relaxed_static_capacity = 6;
    assert_root_comparison(
        upper,
        structure,
        upper,
        different_structure,
        CORE_ROOT_COMPARISON_INCOMPARABLE,
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

static void test_root_comparison_handles_non_finite_heuristics_conservatively(void) {
    CoreRootCandidateStats structure = root_test_stats(CORE_STRUCTURAL_PROOF_SAFE, 12, 4);

    assert_root_comparison(
        root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_EXACT, 1.0, 0),
        structure,
        root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_EXACT, NAN, 0),
        structure,
        CORE_ROOT_COMPARISON_CANDIDATE,
        CORE_ROOT_COMPARISON_HEURISTIC_VALUE
    );
    assert_root_comparison(
        root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_EXACT, INFINITY, 0),
        structure,
        root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_EXACT, INFINITY, 0),
        structure,
        CORE_ROOT_COMPARISON_EQUAL,
        CORE_ROOT_COMPARISON_NOT_COMPARED
    );
    assert_root_comparison(
        root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_EXACT, -INFINITY, 0),
        structure,
        root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_EXACT, -INFINITY, 0),
        structure,
        CORE_ROOT_COMPARISON_EQUAL,
        CORE_ROOT_COMPARISON_NOT_COMPARED
    );
    assert_root_comparison(
        root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_EXACT, INFINITY, 0),
        structure,
        root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_EXACT, -INFINITY, 0),
        structure,
        CORE_ROOT_COMPARISON_INCOMPARABLE,
        CORE_ROOT_COMPARISON_NOT_COMPARED
    );
    assert_root_comparison(
        root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_EXACT, NAN, 0),
        structure,
        root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_EXACT, NAN, 0),
        structure,
        CORE_ROOT_COMPARISON_INCOMPARABLE,
        CORE_ROOT_COMPARISON_NOT_COMPARED
    );
    assert_root_comparison(
        root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_EXACT, INFINITY, 0),
        structure,
        root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_EXACT, NAN, 0),
        structure,
        CORE_ROOT_COMPARISON_INCOMPARABLE,
        CORE_ROOT_COMPARISON_NOT_COMPARED
    );
    assert_root_comparison(
        root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_EXACT, -INFINITY, 0),
        structure,
        root_test_value(CORE_OUTCOME_UNRESOLVED, CORE_VALUE_BOUND_EXACT, NAN, 0),
        structure,
        CORE_ROOT_COMPARISON_INCOMPARABLE,
        CORE_ROOT_COMPARISON_NOT_COMPARED
    );
}

static void test_root_comparison_rejects_invalid_inputs(void) {
    CoreSearchValue value = root_test_value(
        CORE_OUTCOME_UNRESOLVED,
        CORE_VALUE_BOUND_EXACT,
        0.0,
        0
    );
    CoreRootCandidateStats stats = root_test_stats(CORE_STRUCTURAL_PROOF_SAFE, 4, 2);
    CoreRootComparison null_comparison = CoreCompareRootCandidates(
        NULL,
        &stats,
        &value,
        &stats
    );
    assert(null_comparison.ordering == CORE_ROOT_COMPARISON_INCOMPARABLE);
    assert(null_comparison.reason == CORE_ROOT_COMPARISON_NOT_COMPARED);

    CoreSearchValue invalid_value = value;
    invalid_value.outcome = (CoreOutcome)99;
    assert_root_comparison(
        invalid_value,
        stats,
        value,
        stats,
        CORE_ROOT_COMPARISON_INCOMPARABLE,
        CORE_ROOT_COMPARISON_NOT_COMPARED
    );
    invalid_value = value;
    invalid_value.bound = (CoreValueBound)99;
    assert_root_comparison(
        invalid_value,
        stats,
        value,
        stats,
        CORE_ROOT_COMPARISON_INCOMPARABLE,
        CORE_ROOT_COMPARISON_NOT_COMPARED
    );
    CoreRootCandidateStats invalid_stats = stats;
    invalid_stats.structural_proof = (CoreStructuralProofResult)99;
    assert_root_comparison(
        value,
        invalid_stats,
        value,
        stats,
        CORE_ROOT_COMPARISON_INCOMPARABLE,
        CORE_ROOT_COMPARISON_NOT_COMPARED
    );
}

static CoreRootComparisonOrdering root_matrix_compare(
    CoreSearchValue candidate_value,
    CoreRootCandidateStats candidate_stats,
    CoreSearchValue incumbent_value,
    CoreRootCandidateStats incumbent_stats
) {
    return CoreCompareRootCandidates(
        &candidate_value,
        &candidate_stats,
        &incumbent_value,
        &incumbent_stats
    ).ordering;
}

static void test_root_comparison_is_a_consistent_partial_order(void) {
    const CoreOutcome outcomes[] = {
        CORE_OUTCOME_UNRESOLVED,
        CORE_OUTCOME_WIN,
        CORE_OUTCOME_DRAW,
        CORE_OUTCOME_LOSS,
    };
    const CoreValueBound bounds[] = {
        CORE_VALUE_BOUND_EXACT,
        CORE_VALUE_BOUND_LOWER,
        CORE_VALUE_BOUND_UPPER,
    };
    CoreSearchValue values[12];
    CoreRootCandidateStats structures[] = {
        { .structural_proof = CORE_STRUCTURAL_PROOF_SAFE, .relaxed_static_capacity = 1,
          .post_move_length = 2 },
        { .structural_proof = CORE_STRUCTURAL_PROOF_UNSAFE, .relaxed_static_capacity = 8,
          .post_move_length = 2 },
        { .structural_proof = CORE_STRUCTURAL_PROOF_UNKNOWN, .relaxed_static_capacity = 1,
          .post_move_length = 2 },
        { .structural_proof = CORE_STRUCTURAL_PROOF_UNKNOWN, .relaxed_static_capacity = 2,
          .post_move_length = 2 },
    };
    CoreSearchValue matrix_values[48];
    CoreRootCandidateStats matrix_stats[48];
    size_t value_count = 0;
    for (size_t outcome = 0; outcome < sizeof(outcomes) / sizeof(outcomes[0]); outcome++) {
        for (size_t bound = 0; bound < sizeof(bounds) / sizeof(bounds[0]); bound++) {
            values[value_count++] = root_test_value(
                outcomes[outcome],
                bounds[bound],
                0.0,
                1
            );
        }
    }
    size_t matrix_count = 0;
    for (size_t value_index = 0; value_index < value_count; value_index++) {
        for (size_t structure_index = 0;
             structure_index < sizeof(structures) / sizeof(structures[0]);
             structure_index++) {
            matrix_values[matrix_count] = values[value_index];
            matrix_stats[matrix_count] = structures[structure_index];
            matrix_count++;
        }
    }

    for (size_t a = 0; a < matrix_count; a++) {
        for (size_t b = 0; b < matrix_count; b++) {
            CoreRootComparisonOrdering ab = root_matrix_compare(
                matrix_values[a], matrix_stats[a], matrix_values[b], matrix_stats[b]
            );
            CoreRootComparisonOrdering ba = root_matrix_compare(
                matrix_values[b], matrix_stats[b], matrix_values[a], matrix_stats[a]
            );
            if (ab == CORE_ROOT_COMPARISON_CANDIDATE) {
                assert(ba == CORE_ROOT_COMPARISON_INCUMBENT);
            } else if (ab == CORE_ROOT_COMPARISON_INCUMBENT) {
                assert(ba == CORE_ROOT_COMPARISON_CANDIDATE);
            } else {
                assert(ba == ab);
            }
        }
    }

    for (size_t a = 0; a < matrix_count; a++) {
        for (size_t b = 0; b < matrix_count; b++) {
            if (root_matrix_compare(
                    matrix_values[a], matrix_stats[a], matrix_values[b], matrix_stats[b]
                ) != CORE_ROOT_COMPARISON_CANDIDATE) {
                continue;
            }
            for (size_t c = 0; c < matrix_count; c++) {
                if (root_matrix_compare(
                        matrix_values[b], matrix_stats[b], matrix_values[c], matrix_stats[c]
                    ) == CORE_ROOT_COMPARISON_CANDIDATE) {
                    assert(root_matrix_compare(
                        matrix_values[a], matrix_stats[a], matrix_values[c], matrix_stats[c]
                    ) == CORE_ROOT_COMPARISON_CANDIDATE);
                }
            }
        }
    }

    CoreSearchValue upper_unresolved = root_test_value(
        CORE_OUTCOME_UNRESOLVED,
        CORE_VALUE_BOUND_UPPER,
        0.0,
        0
    );
    CoreSearchValue exact_unresolved = root_test_value(
        CORE_OUTCOME_UNRESOLVED,
        CORE_VALUE_BOUND_EXACT,
        0.0,
        0
    );
    CoreSearchValue exact_loss = root_test_value(
        CORE_OUTCOME_LOSS,
        CORE_VALUE_BOUND_EXACT,
        0.0,
        0
    );
    assert(root_matrix_compare(
        upper_unresolved, structures[0], exact_unresolved, structures[1]
    ) == CORE_ROOT_COMPARISON_INCOMPARABLE);
    assert(root_matrix_compare(
        exact_unresolved, structures[1], exact_loss, structures[1]
    ) == CORE_ROOT_COMPARISON_CANDIDATE);
    assert(root_matrix_compare(
        upper_unresolved, structures[0], exact_loss, structures[1]
    ) == CORE_ROOT_COMPARISON_INCOMPARABLE);
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

static void test_production_minimax_structural_precedence_searches_deficient_root(void) {
    Board* board = BoardCreate(11, 11, "standard", 15);
    Coord me_body[] = {
        {7, 8}, {8, 8}, {9, 8}, {10, 8}, {10, 7}, {9, 7},
        {8, 7}, {8, 6}, {7, 6}, {6, 6}, {6, 7}, {6, 8},
    };
    Coord you_body[] = {
        {4, 1}, {3, 1}, {3, 2}, {3, 3}, {4, 3}, {4, 4}, {4, 5},
        {4, 6}, {5, 6}, {5, 5}, {5, 4}, {6, 4}, {6, 3}, {6, 2},
    };
    Coord food[] = {
        {0, 10}, {10, 4}, {0, 0}, {2, 0}, {9, 1},
        {0, 9}, {7, 4}, {3, 10}, {6, 9},
    };
    Snake me = make_snake("me", me_body, 12, 71);
    Snake you = make_snake("you", you_body, 14, 99);
    CoreSearchConfig config = CoreSearchConfigDefault(5000);
    CoreSearchStats stats;
    MoveDirection move = MOVE_INVALID;

    config.fixed_depth = 1;
    memset(&config.weights, 0, sizeof(config.weights));
    config.weights.terminal_win = 1000000.0;
    config.weights.terminal_loss = -1000000.0;
    config.weights.center = 1.0;
    assert(BoardAddSnake(board, &me));
    assert(BoardAddSnake(board, &you));
    for (size_t index = 0; index < sizeof(food) / sizeof(food[0]); ++index) {
        assert(BoardAddFood(board, food[index]));
    }

    assert(CoreMinimaxMoveWithStats(board, "me", config, &move, &stats) == CORE_OK);
    assert(stats.root_candidates[MOVE_DOWN].allowed);
    assert(stats.root_move_score_valid[MOVE_DOWN]);
    assert(stats.root_candidates[MOVE_DOWN].structural_proof == CORE_STRUCTURAL_PROOF_UNKNOWN);
    assert(stats.root_candidates[MOVE_DOWN].relaxed_static_capacity
        < stats.root_candidates[MOVE_DOWN].post_move_length);
    assert(move == MOVE_UP || move == MOVE_LEFT);
    assert(stats.root_candidates[move].structural_proof == CORE_STRUCTURAL_PROOF_SAFE);

    SnakeFree(&me);
    SnakeFree(&you);
    BoardFree(board);
}

static void test_production_minimax_terminal_outcome_precedes_structural_proof(void) {
    Board* board = BoardCreate(6, 6, "standard", 0);
    Coord me_body[] = {{5, 1}, {4, 1}, {3, 1}, {3, 0}, {2, 0}, {1, 0}};
    Coord you_body[] = {{4, 0}, {5, 0}};
    Snake me = make_snake("me", me_body, 6, 90);
    Snake you = make_snake("you", you_body, 2, 90);
    CoreSearchConfig config = CoreSearchConfigDefault(1000);
    CoreSearchStats stats;
    MoveDirection move = MOVE_INVALID;

    config.fixed_depth = 1;
    assert(BoardAddSnake(board, &me));
    assert(BoardAddSnake(board, &you));
    assert(CoreMinimaxMoveWithStats(board, "me", config, &move, &stats) == CORE_OK);
    assert(stats.root_candidates[MOVE_DOWN].minimax_value_valid);
    assert(stats.root_candidates[MOVE_DOWN].minimax_value.outcome == CORE_OUTCOME_WIN);
    assert(stats.root_candidates[MOVE_DOWN].minimax_value.bound == CORE_VALUE_BOUND_EXACT);
    assert(stats.root_candidates[MOVE_UP].structural_proof == CORE_STRUCTURAL_PROOF_SAFE);
    assert(move == MOVE_DOWN);
    assert(stats.root_comparison_reason == CORE_ROOT_COMPARISON_TERMINAL_OUTCOME);

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
#ifdef CORE_ROOT_SELECTION_TESTING
    test_partial_root_frontier_is_permutation_invariant_and_coherent();
    test_exceptional_and_mixed_strict_root_values_are_truthful();
    test_nonfinite_bound_frontiers_do_not_claim_numeric_dominance();
    test_mixed_loss_frontier_keeps_exact_terminal_survival_pairwise();
    test_timeout_snapshot_preserves_complete_or_adopts_coherent_partial();
#endif
    test_root_comparison_orders_exact_outcomes_before_structure();
    test_root_comparison_uses_only_decisive_search_bounds();
    test_root_comparison_matches_expected_interval_table();
    test_root_comparison_applies_structural_lattice_to_unresolved_values();
    test_root_comparison_structural_relation_is_not_a_total_rank();
    test_root_comparison_requires_non_exact_semantic_identity();
    test_root_comparison_prefers_later_exact_loss();
    test_root_comparison_uses_heuristic_only_after_structural_equality();
    test_root_comparison_handles_non_finite_heuristics_conservatively();
    test_root_comparison_rejects_invalid_inputs();
    test_root_comparison_is_a_consistent_partial_order();
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
    test_production_minimax_structural_precedence_searches_deficient_root();
    test_production_minimax_terminal_outcome_precedes_structural_proof();
    test_default_search_config_uses_ladder_policy();
    test_branching_pocket_proves_every_branch_dies();
    test_repeatable_loop_is_structurally_safe();
    test_low_health_loop_is_not_a_safe_structural_witness();
    test_root_hazard_damage_blocks_false_capacity_safety();
    test_extended_horizon_proves_long_corridor_dead_end();
    test_bounded_rectangle_cycle_proves_capacity();
    return 0;
}
