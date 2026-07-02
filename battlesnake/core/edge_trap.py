"""Edge-trapping strategy helpers."""

from __future__ import annotations

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

    # TODO: implement edge-trapping logic for pressuring opponents near walls.
    # Input: board, snake id
    # Output: optional Move
    raise NotImplementedError("Stub — implement me")
