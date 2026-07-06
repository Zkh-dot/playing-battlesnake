from __future__ import annotations

from collections.abc import Iterator

from battlesnake.battlesnake_native import Board, Coord, reachable_space
from battlesnake.training.opponent_model.schema import MOVES, CandidateRow, MoveObservation

MOVE_DELTAS = {
    "up": (0, 1),
    "down": (0, -1),
    "left": (-1, 0),
    "right": (1, 0),
}


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


def _safe_reachable_space(board: Board, snake_id: str, point: Coord) -> float:
    if not board.in_bounds(point):
        return 0.0
    if not board.is_safe(point, snake_id):
        return 0.0
    return float(reachable_space(board, point, snake_id))


def _candidate_point(board: Board, snake_id: str, move: str) -> Coord:
    head = board.head(snake_id)
    dx, dy = MOVE_DELTAS[move]
    return Coord(head.x + dx, head.y + dy)


def candidate_rows(observation: MoveObservation, board: Board) -> Iterator[CandidateRow]:
    snake = board.snakes[observation.snake_id]
    head = board.head(observation.snake_id)
    safe_moves = set(board.safe_moves(observation.snake_id))
    occupied_without_tails = board.occupied(False)
    food = set(board.food)
    hazards = set(board.hazards)

    for move in MOVES:
        point = _candidate_point(board, observation.snake_id, move)
        in_bounds = board.in_bounds(point)
        features = {
            "candidate_move": move,
            "turn": float(observation.turn),
            "board_width": float(board.width),
            "board_height": float(board.height),
            "alive_snakes": float(observation.alive_snakes),
            "snake_rank": float(observation.snake_rank or 999),
            "snake_health": float(snake.health),
            "snake_length": float(snake.length),
            "safe_moves_count": float(len(safe_moves)),
            "candidate_is_safe": 1.0 if move in safe_moves else 0.0,
            "candidate_in_bounds": 1.0 if in_bounds else 0.0,
            "candidate_occupied_without_tails": 1.0 if point in occupied_without_tails else 0.0,
            "candidate_is_food": 1.0 if point in food else 0.0,
            "candidate_is_hazard": 1.0 if point in hazards else 0.0,
            "candidate_to_nearest_food": _nearest_distance(point, food),
            "head_to_nearest_food": _nearest_distance(head, food),
            "candidate_center_distance": _center_distance(board, point) if in_bounds else 99.0,
            "candidate_reachable_space": _safe_reachable_space(board, observation.snake_id, point),
            "adjacent_longer_or_equal_heads": float(
                _adjacent_longer_or_equal_heads(board, observation.snake_id, point)
            ),
            "adjacent_shorter_heads": float(_adjacent_shorter_heads(board, observation.snake_id, point)),
        }
        yield CandidateRow(
            observation_id=observation.observation_id,
            game_id=observation.game_id,
            split=observation.split,
            turn=observation.turn,
            snake_id=observation.snake_id,
            snake_name=observation.snake_name,
            snake_rank=observation.snake_rank,
            candidate_move=move,
            label=1 if move == observation.target_move else 0,
            features=features,
        )
