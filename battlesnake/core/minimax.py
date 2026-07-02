"""Minimax search helpers for duel play."""

from __future__ import annotations

from battlesnake.game import Board
from battlesnake.types import Move


def minimax_move(board: Board, snake_id: str, time_budget_ms: int = 400) -> Move:
    """Return the best move for snake_id under a time-limited minimax search.

    Args:
        board: Current board snapshot.
        snake_id: Snake to optimize for.
        time_budget_ms: Maximum search time in milliseconds.

    Returns:
        Selected move.
    """

    # TODO: implement minimax with alpha-beta pruning for 1v1 games.
    # Input: board, snake id, time budget in milliseconds
    # Output: selected Move
    raise NotImplementedError("Stub — implement me")
