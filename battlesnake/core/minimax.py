"""Minimax search helpers for duel play."""

from __future__ import annotations

from battlesnake.battlesnake_native import minimax_move as native_minimax_move
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

    # TODO: implement minimax with alpha-beta pruning in c-core/core/core_algorithms.c.
    return Move(native_minimax_move(board, snake_id, time_budget_ms))
