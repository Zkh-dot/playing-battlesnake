"""A* pathfinding helpers."""

from __future__ import annotations

from battlesnake.battlesnake_native import shortest_path as native_shortest_path
from battlesnake.game import Board, Coord


def shortest_path(board: Board, start: Coord, goal: Coord, snake_id: str) -> list[Coord]:
    """Return a shortest safe path from start to goal for snake_id.

    Args:
        board: Current board snapshot.
        start: Starting coordinate.
        goal: Target coordinate.
        snake_id: Snake whose movement constraints should be considered.

    Returns:
        A list of coordinates from start to goal, or an empty list if no path exists.
    """

    # TODO: implement A* pathfinding in c-core/core/core_algorithms.c.
    return native_shortest_path(board, start, goal, snake_id)
