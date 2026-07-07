from __future__ import annotations

import json
from pathlib import Path

from battlesnake.battlesnake_native import Board, Coord, Snake, standard_ffa_move
from battlesnake.strategies.standard import StrategyStandard
from tests.test_issue_24_standard_ffa_deepening import corridor_trap_board


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


def tuned_theta() -> dict[str, float]:
    return json.loads(Path("configs/evaluation_weights/standard-ffa-v1-tuned.json").read_text())


def parity_positions() -> list[tuple[str, Board, dict[str, float]]]:
    theta = tuned_theta()
    trap_theta = {**theta, "deepening_interaction_radius": 2.0}
    return [
        (
            "open_corner",
            Board(
                7,
                7,
                [
                    snake("me", [(0, 0), (0, 1), (1, 1)]),
                    snake("north", [(6, 6)]),
                    snake("east", [(6, 0)]),
                ],
            ),
            theta,
        ),
        (
            "larger_space",
            Board(
                7,
                7,
                [
                    snake("me", [(2, 1), (2, 0), (1, 0)]),
                    snake("left-wall", [(1, 2), (1, 3), (1, 4)]),
                    snake("top-wall", [(2, 3), (3, 3), (4, 3)]),
                    snake("far", [(6, 6)]),
                ],
            ),
            theta,
        ),
        (
            "hungry_food",
            Board(
                7,
                7,
                [
                    snake("me", [(2, 2), (2, 1), (2, 0)], health=20),
                    snake("north", [(6, 6)]),
                    snake("east", [(6, 0)]),
                ],
                food=[c(2, 3)],
            ),
            theta,
        ),
        (
            "contested_food",
            Board(
                7,
                7,
                [
                    snake("me", [(1, 2), (1, 1), (1, 0)], health=20),
                    snake("equal", [(3, 2), (3, 1), (3, 0)]),
                    snake("far", [(6, 6)]),
                ],
                food=[c(2, 2)],
            ),
            theta,
        ),
        ("corridor_trap", corridor_trap_board(), trap_theta),
    ]


def test_native_standard_ffa_matches_python_on_parity_corpus() -> None:
    for name, board, theta in parity_positions():
        python_move = str(StrategyStandard(theta=theta).decide(board, "me"))
        native_move = standard_ffa_move(board, "me", 80, theta)

        assert native_move == python_move, name


def test_native_standard_ffa_accepts_default_frozen_tuned_weights() -> None:
    board = parity_positions()[0][1]

    assert standard_ffa_move(board, "me") in {"up", "down", "left", "right"}
