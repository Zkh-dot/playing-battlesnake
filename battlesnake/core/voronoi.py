"""Voronoi-style territory control helpers."""

from __future__ import annotations

from battlesnake.game import Board
from battlesnake.types import Coord


def voronoi_territory(board: Board) -> dict[str, set[Coord]]:
    """Assign reachable cells to the nearest snake head.

    Args:
        board: Current board snapshot.

    Returns:
        Mapping from snake id to the set of cells controlled by that snake.
    """

    # TODO: implement multi-source BFS territory control.
    # Input: board
    # Output: mapping from snake id to controlled coordinate set
    raise NotImplementedError("Stub — implement me")
