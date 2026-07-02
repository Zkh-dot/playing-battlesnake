"""Royale strategy stub."""

from __future__ import annotations

from battlesnake.game import Board
from battlesnake.strategies.base import Strategy
from battlesnake.types import Move


class StrategyRoyale(Strategy):
    """Strategy for Royale games with shrinking hazards."""

    def decide(self, board: Board, snake_id: str) -> Move:
        """Choose a move for a Royale Battlesnake game.

        Args:
            board: Current board snapshot.
            snake_id: Snake to control.

        Returns:
            Selected move.
        """

        # TODO: implement Royale move selection with hazard prediction.
        # Input: board, snake id
        # Output: selected Move
        raise NotImplementedError("Stub — implement me")
