from __future__ import annotations

import json

from battlesnake.battlesnake_native import Board, Coord, Snake
from battlesnake.main import STANDARD_VARIANTS
from battlesnake.strategies.standard import DEFAULT_STANDARD_THETA, StrategyStandard


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


def decide(board: Board, snake_id: str = "me") -> str:
    return str(StrategyStandard().decide(board, snake_id))


def test_theta_is_json_serializable_and_standard_variant_is_registered() -> None:
    json.dumps(DEFAULT_STANDARD_THETA, sort_keys=True)
    assert STANDARD_VARIANTS["standard-v1"] is StrategyStandard


def test_decide_avoids_wall_and_returns_valid_move() -> None:
    board = Board(
        7,
        7,
        [
            snake("me", [(0, 0), (0, 1), (1, 1)]),
            snake("north", [(6, 6)]),
            snake("east", [(6, 0)]),
        ],
    )

    move = decide(board)

    assert move in {"up", "right"}
    assert move in board.safe_moves("me")


def test_decide_prefers_larger_space() -> None:
    board = Board(
        7,
        7,
        [
            snake("me", [(2, 1), (2, 0), (1, 0)]),
            snake("left-wall", [(1, 2), (1, 3), (1, 4)]),
            snake("top-wall", [(2, 3), (3, 3), (4, 3)]),
            snake("far", [(6, 6)]),
        ],
    )

    assert decide(board) == "right"


def test_decide_refuses_pocket_food() -> None:
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

    assert decide(board) != "up"


def test_decide_takes_safe_food_when_hungry() -> None:
    board = Board(
        7,
        7,
        [
            snake("me", [(2, 2), (2, 1), (2, 0)], health=20),
            snake("north", [(6, 6)]),
            snake("east", [(6, 0)]),
        ],
        food=[c(2, 3)],
    )

    assert decide(board) == "up"


def test_decide_avoids_contested_equal_length_food() -> None:
    board = Board(
        7,
        7,
        [
            snake("me", [(1, 2), (1, 1), (1, 0)], health=20),
            snake("equal", [(3, 2), (3, 1), (3, 0)]),
            snake("far", [(6, 6)]),
        ],
        food=[c(2, 2)],
    )

    assert decide(board) != "right"


def test_decide_refuses_suicidal_head_to_head() -> None:
    board = Board(
        7,
        7,
        [
            snake("me", [(2, 2), (2, 1), (2, 0)]),
            snake("equal", [(4, 2), (4, 1), (4, 0)]),
            snake("far", [(6, 6)]),
        ],
    )

    assert decide(board) != "right"
