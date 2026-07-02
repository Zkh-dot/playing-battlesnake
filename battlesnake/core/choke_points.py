"""Choke-point detection helpers."""

from __future__ import annotations

from battlesnake.battlesnake_native import choke_points as native_choke_points
from battlesnake.game import Board, Coord


def choke_points(board: Board, snake_id: str) -> set[Coord]:
    """Return choke-point coordinates relevant to snake_id.

    Args:
        board: Current board snapshot.
        snake_id: Snake whose reachable graph should be analyzed.

    Returns:
        Set of coordinates that behave as graph bridges or narrow passages.
    """

    # TODO: implement Tarjan bridge/articulation analysis in c-core/core/core_algorithms.c.
    return native_choke_points(board, snake_id)
