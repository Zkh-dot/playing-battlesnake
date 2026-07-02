"""Hazard prediction helpers for Royale play."""

from __future__ import annotations

from battlesnake.battlesnake_native import predict_hazards as native_predict_hazards
from battlesnake.game import Board, Coord


def predict_hazards(board: Board, turns_ahead: int = 3) -> set[Coord]:
    """Predict hazard coordinates for future Royale turns.

    Args:
        board: Current board snapshot.
        turns_ahead: Number of turns to predict.

    Returns:
        Set of coordinates expected to be hazardous.
    """

    # TODO: implement hazard zone prediction in c-core/core/core_algorithms.c.
    return native_predict_hazards(board, turns_ahead)
