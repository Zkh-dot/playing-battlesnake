"""Flood-fill territory helpers."""

from __future__ import annotations

from battlesnake.battlesnake_native import reachable_space as native_reachable_space
from battlesnake.game import Board, Coord


def reachable_space(board: Board, start: Coord, snake_id: str) -> int:
    """Return the number of board cells reachable from start for snake_id.

    Args:
        board: Current board snapshot.
        start: Coordinate to begin the search from.
        snake_id: Snake whose movement constraints should be considered.

    Returns:
        Count of reachable safe cells.
    """

    # TODO: implement BFS flood fill in c-core/core/core_algorithms.c.
    return native_reachable_space(board, start, snake_id)
