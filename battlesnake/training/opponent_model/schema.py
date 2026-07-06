from __future__ import annotations

import hashlib
from dataclasses import dataclass
from typing import Any

MOVES = ("up", "down", "left", "right")
Point = tuple[int, int]


@dataclass(frozen=True)
class MoveObservation:
    observation_id: str
    game_id: str
    split: str
    turn: int
    snake_id: str
    snake_name: str
    snake_rank: int | None
    target_move: str
    board_width: int
    board_height: int
    alive_snakes: int


@dataclass(frozen=True)
class CandidateRow:
    observation_id: str
    game_id: str
    split: str
    turn: int
    snake_id: str
    snake_name: str
    snake_rank: int | None
    candidate_move: str
    label: int
    features: dict[str, Any]


def infer_move(previous_head: Point, next_head: Point) -> str | None:
    dx = next_head[0] - previous_head[0]
    dy = next_head[1] - previous_head[1]
    if dx == 0 and dy == 1:
        return "up"
    if dx == 0 and dy == -1:
        return "down"
    if dx == -1 and dy == 0:
        return "left"
    if dx == 1 and dy == 0:
        return "right"
    return None


def deterministic_split(game_id: str) -> str:
    digest = hashlib.sha256(game_id.encode("utf-8")).digest()
    bucket = int.from_bytes(digest[:8], "big") % 100
    if bucket < 70:
        return "train"
    if bucket < 85:
        return "validation"
    return "test"
