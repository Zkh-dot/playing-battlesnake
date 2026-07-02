"""Choke-point detection helpers."""

from __future__ import annotations

from battlesnake.game import Board
from battlesnake.types import Coord


def choke_points(board: Board, snake_id: str) -> set[Coord]:
    """Return choke-point coordinates relevant to snake_id.

    Args:
        board: Current board snapshot.
        snake_id: Snake whose reachable graph should be analyzed.

    Returns:
        Set of coordinates that behave as graph bridges or narrow passages.
    """

    # TODO: implement Tarjan bridge/articulation analysis for choke detection.
    # Input: board, snake id
    # Output: set of choke-point coordinates
    raise NotImplementedError("Stub — implement me")
