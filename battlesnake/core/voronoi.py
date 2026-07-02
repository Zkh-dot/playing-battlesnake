"""Voronoi-style territory control helpers."""

from __future__ import annotations

from battlesnake.battlesnake_native import voronoi_territory as native_voronoi_territory
from battlesnake.game import Board, Coord


def voronoi_territory(board: Board) -> dict[str, set[Coord]]:
    """Assign reachable cells to the nearest snake head.

    Args:
        board: Current board snapshot.

    Returns:
        Mapping from snake id to the set of cells controlled by that snake.
    """

    return native_voronoi_territory(board)
