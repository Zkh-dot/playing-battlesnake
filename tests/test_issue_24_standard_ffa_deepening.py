from __future__ import annotations

from battlesnake.battlesnake_native import Board, Coord, Snake
from battlesnake.strategies.standard import StrategyStandard


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


def corridor_trap_board() -> Board:
    return Board(
        5,
        5,
        [
            snake("me", [(2, 0), (1, 0), (0, 0)], health=20),
            snake("blocks-1", [(4, 4), (1, 1), (1, 2), (1, 3)]),
            snake("blocks-2", [(0, 4), (3, 1), (3, 2), (3, 3)]),
            snake("blocks-3", [(0, 3), (2, 3)]),
        ],
        food=[c(2, 1)],
    )


def test_depth_three_refuses_two_turn_corridor_trap() -> None:
    board = corridor_trap_board()
    theta = {"deepening_interaction_radius": 2.0}

    depth1_move, depth1_record = StrategyStandard(
        theta={**theta, "deepening_enabled": 0.0}
    ).explain_decision(board, "me")
    deep_move, deep_record = StrategyStandard(theta=theta).explain_decision(board, "me")

    assert depth1_move == "up"
    assert deep_move == "right"
    assert deep_record["deepening"]["status"] == "completed"
    assert deep_record["deepening"]["refused_traps"] == 1

    up = next(candidate for candidate in deep_record["candidates"] if candidate["move"] == "up")
    assert up["depth1_score"] == next(
        candidate["score"] for candidate in depth1_record["candidates"] if candidate["move"] == "up"
    )
    assert up["deepening"]["active_opponents"] == []
    assert up["deepening"]["frozen_opponents"] == ["blocks-1", "blocks-2", "blocks-3"]
    assert up["deepening"]["refused_trap"] is True
    assert up["score"] <= -900_000


def test_relevance_filter_keeps_only_nearby_heads_active() -> None:
    board = Board(
        7,
        7,
        [
            snake("me", [(2, 2), (2, 1), (2, 0)]),
            snake("near", [(4, 2), (4, 1), (4, 0)]),
            snake("far", [(6, 6), (6, 5), (6, 4)]),
        ],
    )

    _move, record = StrategyStandard(
        theta={
            "deepening_interaction_radius": 3.0,
            "deepening_top_candidates": 1.0,
        }
    ).explain_decision(board, "me")

    deepened = next(candidate for candidate in record["candidates"] if candidate.get("deepening"))
    assert deepened["deepening"]["active_opponents"] == ["near"]
    assert deepened["deepening"]["frozen_opponents"] == ["far"]
    assert deepened["deepening"]["frozen_interaction_risk"]["checked"] == 1


def test_deepening_timeout_keeps_depth_one_result() -> None:
    board = corridor_trap_board()
    theta = {
        "deepening_interaction_radius": 2.0,
        "deepening_margin_ms": 10_000.0,
    }

    depth1_move, _depth1_record = StrategyStandard(
        theta={**theta, "deepening_enabled": 0.0}
    ).explain_decision(board, "me")
    timeout_move, timeout_record = StrategyStandard(theta=theta).explain_decision(board, "me")

    assert timeout_move == depth1_move == "up"
    assert timeout_record["deepening"]["status"] == "timeout_depth1_fallback"
    assert all(
        candidate.get("deepening", {}).get("status") == "depth1_fallback"
        for candidate in timeout_record["candidates"]
        if candidate.get("score") is not None
    )
