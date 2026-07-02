"""Board evaluation helpers."""

from __future__ import annotations

from battlesnake.battlesnake_native import evaluate as native_evaluate
from battlesnake.game import Board


WEIGHTS: dict[str, float] = {
    "health": 1.0,
    "length": 1.5,
    "space": 2.0,
    "food": 1.0,
    "hazard": -2.0,
    "center": 0.25,
}


def evaluate(board: Board, snake_id: str) -> float:
    """Return a weighted board score from snake_id's perspective.

    Args:
        board: Current board snapshot.
        snake_id: Snake to evaluate for.

    Returns:
        Floating-point utility score where larger is better for snake_id.
    """

    # TODO: implement weighted evaluation in c-core/core/core_algorithms.c.
    return native_evaluate(board, snake_id)
