"""Edge-trapping strategy helpers."""

from __future__ import annotations

from battlesnake.battlesnake_native import edge_trap_move as native_edge_trap_move
from battlesnake.game import Board
from battlesnake.types import Move


def edge_trap_move(board: Board, snake_id: str) -> Move | None:
    """Return a move that attempts to trap an opponent against an edge.

    Args:
        board: Current board snapshot.
        snake_id: Snake to choose a trapping move for.

    Returns:
        A Move when an edge trap is available, otherwise None.
    """

    # TODO: implement edge-trapping logic in c-core/core/core_algorithms.c.
    move = native_edge_trap_move(board, snake_id)
    return Move(move) if move is not None else None
