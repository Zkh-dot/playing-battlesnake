"""Flood-fill territory helpers."""

from __future__ import annotations

from battlesnake.game import Board
from battlesnake.types import Coord


def reachable_space(board: Board, start: Coord, snake_id: str) -> int:
    """Return the number of board cells reachable from start for snake_id.

    Args:
        board: Current board snapshot.
        start: Coordinate to begin the search from.
        snake_id: Snake whose movement constraints should be considered.

    Returns:
        Count of reachable safe cells.
    """

    # TODO: implement BFS flood fill that counts reachable safe cells.
    # Input: board, start coordinate, snake id
    # Output: integer reachable cell count
    raise NotImplementedError("Stub — implement me")
