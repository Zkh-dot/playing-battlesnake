"""Pydantic models for the Battlesnake API payloads."""

from __future__ import annotations

from enum import Enum
from typing import Any

from pydantic import BaseModel, Field


class Coord(BaseModel):
    """A coordinate on the Battlesnake board."""

    x: int
    y: int

    class Config:
        frozen = True

    def __hash__(self) -> int:
        return hash((self.x, self.y))


class Move(str, Enum):
    """Legal Battlesnake movement directions."""

    UP = "up"
    DOWN = "down"
    LEFT = "left"
    RIGHT = "right"


class Customizations(BaseModel):
    """Optional visual customizations included in Battlesnake payloads."""

    color: str | None = None
    head: str | None = None
    tail: str | None = None


class Snake(BaseModel):
    """A Battlesnake player state."""

    id: str
    name: str = ""
    health: int = 100
    body: list[Coord] = Field(default_factory=list)
    latency: str | None = None
    head: Coord | None = None
    length: int | None = None
    shout: str | None = None
    squad: str | None = None
    customizations: Customizations | None = None

    def model_post_init(self, __context: Any) -> None:
        if self.head is None and self.body:
            self.head = self.body[0]
        if self.length is None:
            self.length = len(self.body)


class Ruleset(BaseModel):
    """Battlesnake ruleset metadata."""

    name: str = "standard"
    version: str | None = None
    settings: dict[str, Any] = Field(default_factory=dict)


class Game(BaseModel):
    """Battlesnake game metadata."""

    id: str
    ruleset: Ruleset = Field(default_factory=Ruleset)
    map: str | None = None
    timeout: int | None = None
    source: str | None = None


class BoardState(BaseModel):
    """Current board contents from the Battlesnake move payload."""

    height: int
    width: int
    food: list[Coord] = Field(default_factory=list)
    hazards: list[Coord] = Field(default_factory=list)
    snakes: list[Snake] = Field(default_factory=list)


class GameState(BaseModel):
    """Full Battlesnake turn payload."""

    game: Game
    turn: int = 0
    board: BoardState
    you: Snake
