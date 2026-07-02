"""Base strategy interface."""

from __future__ import annotations

from abc import ABC, abstractmethod

from battlesnake.game import Board
from battlesnake.types import Move


class Strategy(ABC):
    """Abstract interface for Battlesnake strategies."""

    @abstractmethod
    def decide(self, board: Board, snake_id: str) -> Move:
        """Return a move for snake_id on board."""
