"""Baseline strategy: first immediately safe move."""

from __future__ import annotations

from battlesnake.core.duel_profile import duel_profile_fallback
from battlesnake.game import Board
from battlesnake.strategies.base import Strategy
from battlesnake.types import Move


class StrategyFirstSafe(Strategy):
    """Pick the first immediately safe move, or up when no safe move exists.

    This mirrors the production native-server fallback and serves as the
    baseline the Standard FFA dev snake variants are compared against.
    """

    def decide(self, board: Board, snake_id: str) -> Move:
        """Return the first safe move for snake_id, or up when trapped."""

        safe_moves = board.safe_moves(snake_id)
        if safe_moves:
            return Move(safe_moves[0])
        duel_move = duel_profile_fallback(board, snake_id)
        return duel_move if duel_move is not None else Move.UP
