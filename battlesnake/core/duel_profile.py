"""Shared least-bad fallback selection for duel boards."""

from __future__ import annotations

from battlesnake.battlesnake_native import duel_root_profile
from battlesnake.game import Board
from battlesnake.types import Move


def duel_profile_fallback(board: Board, snake_id: str) -> Move | None:
    """Return a profile-backed duel fallback, or None outside a duel."""

    if len(board.snakes) != 2:
        return None

    profile = duel_root_profile(board, snake_id)
    ordered_moves = list(Move)
    for move in ordered_moves:
        if int(profile[move.value]["alive_reply_count"]) > 0:
            return move

    for move in ordered_moves:
        outcomes = profile[move.value]["reply_outcomes"].values()
        if outcomes and all(outcome == "draw" for outcome in outcomes):
            return move

    return ordered_moves[0]
