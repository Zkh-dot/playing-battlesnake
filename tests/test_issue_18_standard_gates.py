from __future__ import annotations

import json

from battlesnake.battlesnake_native import Board, Coord, Snake
from battlesnake.strategies.standard_gates import (
    DeathClass,
    StandardGateResult,
    classify_standard_ffa_candidates,
)


def c(x: int, y: int) -> Coord:
    return Coord(x, y)


def snake(
    snake_id: str,
    body: list[tuple[int, int]],
    *,
    health: int = 100,
) -> Snake:
    return Snake(
        id=snake_id,
        name=snake_id,
        health=health,
        body=[c(x, y) for x, y in body],
    )


def candidate(result: StandardGateResult, move: str):
    return next(item for item in result.candidates if item.move == move)


def test_classifies_wall_death() -> None:
    board = Board(
        5,
        5,
        [
            snake("me", [(0, 0), (0, 1), (1, 1)]),
            snake("other", [(4, 4)]),
            snake("third", [(4, 0)]),
        ],
    )

    left = candidate(classify_standard_ffa_candidates(board, "me"), "left")

    assert left.death_class == DeathClass.WALL
    assert left.terminal
    assert not left.eligible
    assert not left.in_bounds


def test_classifies_body_death_without_confusing_it_with_head_to_head() -> None:
    board = Board(
        5,
        5,
        [
            snake("me", [(1, 1), (1, 0)]),
            snake("body-owner", [(4, 4), (2, 1), (2, 0)]),
            snake("third", [(0, 4)]),
        ],
    )

    right = candidate(classify_standard_ffa_candidates(board, "me"), "right")

    assert right.death_class == DeathClass.BODY
    assert right.terminal
    assert right.candidate_occupied


def test_classifies_self_death() -> None:
    board = Board(
        5,
        5,
        [
            snake("me", [(1, 1), (2, 1), (2, 0)]),
            snake("other", [(4, 4)]),
            snake("third", [(0, 4)]),
        ],
    )

    right = candidate(classify_standard_ffa_candidates(board, "me"), "right")

    assert right.death_class == DeathClass.SELF
    assert right.terminal
    assert right.candidate_occupied


def test_classifies_equal_length_head_to_head_death() -> None:
    board = Board(
        5,
        5,
        [
            snake("me", [(1, 1), (1, 0), (0, 0)]),
            snake("equal", [(3, 1), (3, 0), (4, 0)]),
            snake("third", [(0, 4)]),
        ],
    )

    right = candidate(classify_standard_ffa_candidates(board, "me"), "right")

    assert right.death_class == DeathClass.HEAD_TO_HEAD_LOSING
    assert right.terminal
    assert not right.safe_by_board_rules


def test_classifies_hazard_starvation_death() -> None:
    board = Board(
        5,
        5,
        [
            snake("me", [(1, 1), (1, 0)], health=16),
            snake("other", [(4, 4)]),
            snake("third", [(0, 4)]),
        ],
        hazards=[c(2, 1)],
        hazard_damage=15,
    )

    right = candidate(classify_standard_ffa_candidates(board, "me"), "right")

    assert right.death_class == DeathClass.HAZARD_STARVATION
    assert right.terminal
    assert right.enters_hazard


def test_food_on_hazard_can_prevent_starvation_classification() -> None:
    board = Board(
        5,
        5,
        [
            snake("me", [(1, 1), (1, 0)], health=10),
            snake("other", [(4, 4)]),
            snake("third", [(0, 4)]),
        ],
        food=[c(2, 1)],
        hazards=[c(2, 1)],
        hazard_damage=14,
    )

    right = candidate(classify_standard_ffa_candidates(board, "me"), "right")

    assert right.candidate_food
    assert right.enters_hazard
    assert right.death_class is None
    assert right.eligible


def test_classifies_zero_escape_as_trapped_next_turn() -> None:
    board = Board(
        5,
        5,
        [
            snake("me", [(2, 1), (0, 0), (0, 1)]),
            snake("left-blocker", [(1, 2), (1, 1)]),
            snake("right-blocker", [(3, 2), (3, 1)]),
            snake("top-blocker", [(2, 3), (1, 3)]),
        ],
    )

    up = candidate(classify_standard_ffa_candidates(board, "me"), "up")

    assert up.death_class == DeathClass.TRAPPED_NEXT_TURN
    assert not up.terminal
    assert up.severe
    assert not up.eligible
    assert up.safe_by_board_rules
    assert up.immediate_safe_move_count_after == 0


def test_tail_vacation_chase_can_remain_eligible() -> None:
    board = Board(
        4,
        3,
        [
            snake("me", [(0, 1), (0, 0), (1, 0)]),
            snake("tail", [(2, 1), (1, 1)]),
            snake("third", [(3, 2)]),
        ],
    )

    right = candidate(classify_standard_ffa_candidates(board, "me"), "right")

    assert right.candidate_occupied
    assert right.safe_by_board_rules
    assert right.death_class is None
    assert right.eligible


def test_food_on_trapped_cell_keeps_food_signal_and_trap_classification() -> None:
    board = Board(
        5,
        5,
        [
            snake("me", [(2, 1), (0, 0), (0, 1)]),
            snake("left-blocker", [(1, 2), (1, 1)]),
            snake("right-blocker", [(3, 2), (3, 1)]),
            snake("top-blocker", [(2, 3), (1, 3)]),
        ],
        food=[c(2, 2)],
    )

    up = candidate(classify_standard_ffa_candidates(board, "me"), "up")

    assert up.candidate_food
    assert up.death_class == DeathClass.TRAPPED_NEXT_TURN
    assert up.severe


def test_result_contract_is_deterministic_and_serializable() -> None:
    board = Board(
        5,
        5,
        [
            snake("me", [(2, 2), (2, 1), (2, 0)]),
            snake("northwest", [(0, 4)]),
            snake("northeast", [(4, 4)]),
        ],
    )

    result = classify_standard_ffa_candidates(board, "me")

    assert [item.move for item in result.candidates] == ["up", "down", "left", "right"]
    assert [item.move for item in result.eligible_candidates] == ["up", "left", "right"]
    json.dumps(result.to_dict())
    for item in result.candidates:
        json.dumps(item.to_dict())


def test_least_bad_fallback_prefers_severity_then_reachable_space() -> None:
    board = Board(
        5,
        5,
        [
            snake("me", [(2, 0), (1, 0), (0, 0)]),
            snake("left-blocker", [(1, 1), (1, 2)]),
            snake("right-blocker", [(3, 1), (3, 2)]),
            snake("top-blocker", [(2, 2), (4, 2)]),
            snake("right-body", [(4, 4), (3, 0), (4, 3)]),
        ],
    )

    result = classify_standard_ffa_candidates(board, "me")

    assert result.eligible_candidates == ()
    assert candidate(result, "up").death_class == DeathClass.TRAPPED_NEXT_TURN
    assert candidate(result, "left").death_class == DeathClass.SELF
    assert candidate(result, "right").death_class == DeathClass.BODY
    assert candidate(result, "down").death_class == DeathClass.WALL
    assert result.least_bad_candidate.move == "up"
