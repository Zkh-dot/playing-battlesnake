"""Runtime-safe opponent-model feature extraction."""

from __future__ import annotations

from collections.abc import Iterator
from dataclasses import dataclass
from typing import Any

from battlesnake.battlesnake_native import Board, Coord, reachable_space

MOVES = ("up", "down", "left", "right")
MOVE_DELTAS = {
    "up": (0, 1),
    "down": (0, -1),
    "left": (-1, 0),
    "right": (1, 0),
}


@dataclass(frozen=True)
class RuntimeCandidateFeatures:
    candidate_move: str
    features: dict[str, Any]


def candidate_feature_rows(
    *,
    board: Board,
    snake_id: str,
    turn: int,
    snake_rank: int | None,
    alive_snakes: int | None = None,
) -> Iterator[RuntimeCandidateFeatures]:
    snake = board.snakes[snake_id]
    head = board.head(snake_id)
    safe_moves = set(board.safe_moves(snake_id))
    occupied_without_tails = board.occupied(False)
    food = set(board.food)
    hazards = set(board.hazards)
    rank_value = 999.0 if snake_rank is None else float(snake_rank)
    alive_count = len(board.snakes) if alive_snakes is None else alive_snakes

    for move in MOVES:
        point = _candidate_point(board, snake_id, move)
        in_bounds = board.in_bounds(point)
        is_safe = move in safe_moves
        yield RuntimeCandidateFeatures(
            candidate_move=move,
            features={
                "candidate_move": move,
                "turn": float(turn),
                "board_width": float(board.width),
                "board_height": float(board.height),
                "alive_snakes": float(alive_count),
                "snake_rank": rank_value,
                "snake_health": float(snake.health),
                "snake_length": float(snake.length),
                "safe_moves_count": float(len(safe_moves)),
                "candidate_is_safe": 1.0 if is_safe else 0.0,
                "candidate_in_bounds": 1.0 if in_bounds else 0.0,
                "candidate_occupied_without_tails": 1.0 if point in occupied_without_tails else 0.0,
                "candidate_is_food": 1.0 if point in food else 0.0,
                "candidate_is_hazard": 1.0 if point in hazards else 0.0,
                "candidate_to_nearest_food": _nearest_distance(point, food),
                "head_to_nearest_food": _nearest_distance(head, food),
                "candidate_center_distance": _center_distance(board, point) if in_bounds else 99.0,
                "candidate_reachable_space": _safe_reachable_space(board, snake_id, point, is_safe),
                "adjacent_longer_or_equal_heads": float(_adjacent_longer_or_equal_heads(board, snake_id, point)),
                "adjacent_shorter_heads": float(_adjacent_shorter_heads(board, snake_id, point)),
            },
        )


def _distance(a: Coord, b: Coord) -> int:
    return abs(a.x - b.x) + abs(a.y - b.y)


def _nearest_distance(point: Coord, targets: set[Coord]) -> float:
    if not targets:
        return 99.0
    return float(min(_distance(point, target) for target in targets))


def _center_distance(board: Board, point: Coord) -> float:
    center_x = (board.width - 1) / 2.0
    center_y = (board.height - 1) / 2.0
    return abs(point.x - center_x) + abs(point.y - center_y)


def _adjacent_longer_or_equal_heads(board: Board, snake_id: str, point: Coord) -> int:
    me = board.snakes[snake_id]
    count = 0
    for other_id, other in board.snakes.items():
        if other_id == snake_id or other.head is None:
            continue
        if other.length >= me.length and _distance(point, other.head) == 1:
            count += 1
    return count


def _adjacent_shorter_heads(board: Board, snake_id: str, point: Coord) -> int:
    me = board.snakes[snake_id]
    count = 0
    for other_id, other in board.snakes.items():
        if other_id == snake_id or other.head is None:
            continue
        if other.length < me.length and _distance(point, other.head) == 1:
            count += 1
    return count


def _safe_reachable_space(board: Board, snake_id: str, point: Coord, is_safe: bool) -> float:
    if not is_safe:
        return 0.0
    return float(reachable_space(board, point, snake_id))


def _candidate_point(board: Board, snake_id: str, move: str) -> Coord:
    head = board.head(snake_id)
    dx, dy = MOVE_DELTAS[move]
    return Coord(head.x + dx, head.y + dy)
