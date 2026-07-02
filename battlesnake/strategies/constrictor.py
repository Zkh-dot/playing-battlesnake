"""Constrictor strategy stub."""

from __future__ import annotations

from battlesnake.game import Board
from battlesnake.strategies.base import Strategy
from battlesnake.types import Move


class StrategyConstrictor(Strategy):
    """Strategy for constrictor games where tails never shrink."""

    def decide(self, board: Board, snake_id: str) -> Move:
        """Choose a move for a constrictor Battlesnake game.

        Args:
            board: Current board snapshot.
            snake_id: Snake to control.

        Returns:
            Selected move.
        """

        # TODO: implement constrictor move selection for permanently growing bodies.
        # Input: board, snake id
        # Output: selected Move
        raise NotImplementedError("Stub — implement me")
