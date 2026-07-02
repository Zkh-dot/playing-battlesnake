"""Duel strategy stub."""

from __future__ import annotations

from battlesnake.game import Board
from battlesnake.strategies.base import Strategy
from battlesnake.types import Move


class StrategyDuel(Strategy):
    """Strategy for two-player duel games."""

    def decide(self, board: Board, snake_id: str) -> Move:
        """Choose a move for a 1v1 Battlesnake game.

        Args:
            board: Current board snapshot.
            snake_id: Snake to control.

        Returns:
            Selected move, typically from a minimax search.
        """

        # TODO: implement duel move selection using minimax.
        # Input: board, snake id
        # Output: selected Move
        raise NotImplementedError("Stub — implement me")
