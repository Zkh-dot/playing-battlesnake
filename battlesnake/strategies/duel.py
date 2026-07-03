"""Duel strategy."""

from __future__ import annotations

from battlesnake.core.minimax import minimax_move
from battlesnake.game import Board
from battlesnake.strategies.base import Strategy
from battlesnake.types import Move


class StrategyDuel(Strategy):
    """Strategy for two-player duel games."""

    time_budget_ms = 400

    def __init__(self, weights: dict[str, float] | None = None, time_budget_ms: int | None = None) -> None:
        """Create a duel strategy with optional evaluation-weight overrides."""

        self.weights = weights
        if time_budget_ms is not None:
            self.time_budget_ms = time_budget_ms

    def decide(self, board: Board, snake_id: str) -> Move:
        """Choose a move for a 1v1 Battlesnake game.

        Args:
            board: Current board snapshot.
            snake_id: Snake to control.

        Returns:
            Selected move, typically from a minimax search.
        """

        if snake_id not in board.snakes:
            raise KeyError(f"snake id not found: {snake_id}")

        if len(board.snakes) != 2:
            return self._fallback_move(board, snake_id)

        try:
            return minimax_move(board, snake_id, self.time_budget_ms, self.weights)
        except (RuntimeError, ValueError):
            return self._fallback_move(board, snake_id)

    @staticmethod
    def _fallback_move(board: Board, snake_id: str) -> Move:
        """Return the first safe move, or up when no safe move exists."""

        safe_moves = board.safe_moves(snake_id)
        return Move(safe_moves[0]) if safe_moves else Move.UP
