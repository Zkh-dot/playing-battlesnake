"""A* pathfinding helpers."""

from __future__ import annotations

from battlesnake.game import Board
from battlesnake.types import Coord


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

    # TODO: implement A* pathfinding over safe board cells.
    # Input: board, start coordinate, goal coordinate, snake id
    # Output: list of coordinates forming the path
    raise NotImplementedError("Stub — implement me")
