"""Standard free-for-all strategy stub."""

from __future__ import annotations

from battlesnake.game import Board
from battlesnake.strategies.base import Strategy
from battlesnake.types import Move


class StrategyStandard(Strategy):
    """Strategy for standard multi-snake free-for-all games."""

    def decide(self, board: Board, snake_id: str) -> Move:
        """Choose a move for a standard Battlesnake game.

        Args:
            board: Current board snapshot.
            snake_id: Snake to control.

        Returns:
            Selected move.
        """

        # TODO: implement standard free-for-all move selection.
        # Input: board, snake id
        # Output: selected Move
        raise NotImplementedError("Stub — implement me")
