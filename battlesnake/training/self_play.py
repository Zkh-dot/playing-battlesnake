"""Deterministic self-play simulation for simple weight tuning."""

from __future__ import annotations

from collections import deque
from random import Random
from typing import Any

from battlesnake.game import Board
from battlesnake.types import Coord, Move, Snake


DEFAULT_WEIGHTS: dict[str, float] = {
    "health": 0.04,
    "length": 1.2,
    "space": 0.35,
    "food": 6.0,
    "hazard": -12.0,
    "center": 0.25,
}

DEFAULT_CONFIG: dict[str, Any] = {
    "width": 11,
    "height": 11,
    "snake_count": 4,
    "max_turns": 200,
    "min_food": 3,
    "seed": None,
    "ruleset_name": "standard",
    "hazard_damage": 15,
    "learning_rate": 0.02,
    "weights": DEFAULT_WEIGHTS,
}


def play_game(config: dict[str, Any] | None = None) -> dict[str, Any]:
    """Play one self-play game for lightweight weight tuning.

    Args:
        config: Optional settings. Supported keys are width, height,
            snake_count, max_turns, min_food, seed, ruleset_name,
            hazard_damage, learning_rate, weights, food, and hazards.

    Returns:
        Dictionary containing winner, turns, final snake states, per-snake
        statistics, and updated heuristic weights.
    """

    resolved = _resolve_config(config)
    rng = Random(resolved["seed"])
    weights = dict(resolved["weights"])
    stats = _initial_stats(resolved["snake_count"])
    snakes = _initial_snakes(resolved["width"], resolved["height"], resolved["snake_count"])
    food = set(_coords_from_config(resolved.get("food")))
    hazards = set(_coords_from_config(resolved.get("hazards")))

    board = Board(
        width=resolved["width"],
        height=resolved["height"],
        snakes=snakes,
        food=food,
        hazards=hazards,
        ruleset_name=resolved["ruleset_name"],
        hazard_damage=resolved["hazard_damage"],
    )
    board = _refill_food(board, rng, resolved["min_food"])

    turn = 0
    winner_id: str | None = None
    elimination_turns: dict[str, int] = {}

    while turn < resolved["max_turns"] and len(board.snakes) > 1:
        turn += 1
        moves = {
            snake_id: _choose_move(board, snake_id, weights, rng)
            for snake_id in sorted(board.snakes)
        }
        _record_turn_stats(board, moves, stats)
        previous_alive = set(board.snakes)
        board = board.clone_and_apply(moves)
        board = _refill_food(board, rng, resolved["min_food"])

        for eliminated in sorted(previous_alive - set(board.snakes)):
            elimination_turns[eliminated] = turn

    if board.snakes:
        winner_id = _winner(board)

    updated_weights = _updated_weights(weights, stats, winner_id, resolved["learning_rate"])

    return {
        "winner": winner_id,
        "turns": turn,
        "reason": _finish_reason(turn, resolved["max_turns"], board),
        "weights": updated_weights,
        "initial_config": {
            key: value
            for key, value in resolved.items()
            if key not in {"food", "hazards", "weights"}
        },
        "final_snakes": {
            snake_id: {
                "health": snake.health,
                "length": snake.length or len(snake.body),
                "head": _coord_to_dict(board.head(snake_id)),
                "body": [_coord_to_dict(coord) for coord in snake.body],
            }
            for snake_id, snake in sorted(board.snakes.items())
        },
        "elimination_turns": elimination_turns,
        "stats": stats,
    }


def _resolve_config(config: dict[str, Any] | None) -> dict[str, Any]:
    resolved = dict(DEFAULT_CONFIG)
    if config:
        resolved.update(config)

    resolved["weights"] = dict(DEFAULT_WEIGHTS) | dict(resolved.get("weights") or {})
    resolved["width"] = int(resolved["width"])
    resolved["height"] = int(resolved["height"])
    resolved["snake_count"] = int(resolved["snake_count"])
    resolved["max_turns"] = int(resolved["max_turns"])
    resolved["min_food"] = int(resolved["min_food"])
    resolved["hazard_damage"] = int(resolved["hazard_damage"])
    resolved["learning_rate"] = float(resolved["learning_rate"])

    if resolved["snake_count"] < 2 or resolved["snake_count"] > 4:
        raise ValueError("snake_count must be between 2 and 4")
    if resolved["width"] < 7 or resolved["height"] < 7:
        raise ValueError("width and height must both be at least 7")
    if resolved["max_turns"] < 1:
        raise ValueError("max_turns must be positive")
    if resolved["min_food"] < 0:
        raise ValueError("min_food must be non-negative")

    return resolved


def _initial_snakes(width: int, height: int, snake_count: int) -> dict[str, Snake]:
    starts = [
        [Coord(x=1, y=1), Coord(x=1, y=0), Coord(x=0, y=0)],
        [
            Coord(x=width - 2, y=height - 2),
            Coord(x=width - 2, y=height - 1),
            Coord(x=width - 1, y=height - 1),
        ],
        [Coord(x=width - 2, y=1), Coord(x=width - 1, y=1), Coord(x=width - 1, y=0)],
        [Coord(x=1, y=height - 2), Coord(x=0, y=height - 2), Coord(x=0, y=height - 1)],
    ]
    return {
        f"snake-{index}": Snake(
            id=f"snake-{index}",
            name=f"Training Snake {index}",
            health=100,
            body=starts[index],
            head=starts[index][0],
            length=len(starts[index]),
        )
        for index in range(snake_count)
    }


def _initial_stats(snake_count: int) -> dict[str, dict[str, int]]:
    return {
        f"snake-{index}": {
            "turns_alive": 0,
            "food_eaten": 0,
            "hazard_entries": 0,
            "safe_moves_seen": 0,
        }
        for index in range(snake_count)
    }


def _coords_from_config(raw: Any) -> list[Coord]:
    if not raw:
        return []

    coords: list[Coord] = []
    for value in raw:
        if isinstance(value, Coord):
            coords.append(value)
        elif isinstance(value, dict):
            coords.append(Coord(x=int(value["x"]), y=int(value["y"])))
        else:
            x, y = value
            coords.append(Coord(x=int(x), y=int(y)))
    return coords


def _choose_move(board: Board, snake_id: str, weights: dict[str, float], rng: Random) -> Move:
    safe_moves = board.safe_moves(snake_id)
    candidates = safe_moves or list(Move)
    scored = [
        (_score_move(board, snake_id, move, weights, rng), move)
        for move in candidates
    ]
    scored.sort(key=lambda item: item[0], reverse=True)
    return scored[0][1]


def _score_move(
    board: Board,
    snake_id: str,
    move: Move,
    weights: dict[str, float],
    rng: Random,
) -> float:
    snake = board.snakes[snake_id]
    next_head = board.step(board.head(snake_id), move)
    if not board.in_bounds(next_head):
        return -1_000_000.0 + rng.random()

    score = rng.random() * 0.001
    score += weights["space"] * _reachable_space(board, next_head, snake_id)
    score += weights["center"] * _center_score(board, next_head)
    score += weights["health"] * snake.health
    score += weights["length"] * (snake.length or len(snake.body))

    if next_head in board.hazards:
        score += weights["hazard"]
    if next_head in board.food:
        score += weights["food"] * 2.0
    else:
        distance = _nearest_food_distance(board, next_head)
        if distance is not None:
            score += weights["food"] / (distance + 1)

    occupied = board.occupied(include_tails=False)
    if next_head in occupied:
        score -= 5000.0

    own_length = snake.length or len(snake.body)
    for other_id, other in board.snakes.items():
        if other_id == snake_id:
            continue
        enemy_head = board.head(other_id)
        if _manhattan(next_head, enemy_head) == 1 and (other.length or len(other.body)) >= own_length:
            score -= 50.0

    return score


def _reachable_space(board: Board, start: Coord, snake_id: str) -> int:
    if not board.in_bounds(start):
        return 0

    blocked = board.occupied(include_tails=False)
    blocked.discard(board.head(snake_id))
    if start in blocked:
        return 0

    queue: deque[Coord] = deque([start])
    seen = {start}
    while queue:
        coord = queue.popleft()
        for move in Move:
            nxt = board.step(coord, move)
            if nxt in seen or not board.in_bounds(nxt) or nxt in blocked:
                continue
            seen.add(nxt)
            queue.append(nxt)
    return len(seen)


def _center_score(board: Board, coord: Coord) -> float:
    center_x = (board.width - 1) / 2
    center_y = (board.height - 1) / 2
    max_distance = center_x + center_y
    if max_distance == 0:
        return 0.0
    distance = abs(coord.x - center_x) + abs(coord.y - center_y)
    return max_distance - distance


def _nearest_food_distance(board: Board, coord: Coord) -> int | None:
    if not board.food:
        return None
    return min(_manhattan(coord, food) for food in board.food)


def _manhattan(left: Coord, right: Coord) -> int:
    return abs(left.x - right.x) + abs(left.y - right.y)


def _record_turn_stats(
    board: Board,
    moves: dict[str, Move],
    stats: dict[str, dict[str, int]],
) -> None:
    for snake_id, move in moves.items():
        next_head = board.step(board.head(snake_id), move)
        snake_stats = stats[snake_id]
        snake_stats["turns_alive"] += 1
        snake_stats["safe_moves_seen"] += len(board.safe_moves(snake_id))
        if next_head in board.food:
            snake_stats["food_eaten"] += 1
        if next_head in board.hazards:
            snake_stats["hazard_entries"] += 1


def _refill_food(board: Board, rng: Random, min_food: int) -> Board:
    if len(board.food) >= min_food:
        return board

    occupied = board.occupied(include_tails=True)
    food = set(board.food)
    available = [
        Coord(x=x, y=y)
        for x in range(board.width)
        for y in range(board.height)
        if Coord(x=x, y=y) not in occupied
        and Coord(x=x, y=y) not in board.hazards
        and Coord(x=x, y=y) not in food
    ]
    rng.shuffle(available)

    while len(food) < min_food and available:
        food.add(available.pop())

    return Board(
        width=board.width,
        height=board.height,
        snakes=board.snakes,
        food=food,
        hazards=board.hazards,
        ruleset_name=board.ruleset_name,
        hazard_damage=board.hazard_damage,
    )


def _winner(board: Board) -> str:
    return max(
        board.snakes,
        key=lambda snake_id: (
            board.snakes[snake_id].length or len(board.snakes[snake_id].body),
            board.snakes[snake_id].health,
        ),
    )


def _updated_weights(
    weights: dict[str, float],
    stats: dict[str, dict[str, int]],
    winner_id: str | None,
    learning_rate: float,
) -> dict[str, float]:
    updated = dict(weights)
    if not winner_id or learning_rate == 0:
        return updated

    winner_stats = stats[winner_id]
    turns_alive = max(winner_stats["turns_alive"], 1)
    food_rate = winner_stats["food_eaten"] / turns_alive
    average_safe_moves = winner_stats["safe_moves_seen"] / turns_alive
    hazard_rate = winner_stats["hazard_entries"] / turns_alive

    updated["food"] += learning_rate * food_rate
    updated["space"] += learning_rate * average_safe_moves
    updated["hazard"] -= learning_rate * hazard_rate
    updated["health"] += learning_rate * 0.1
    updated["length"] += learning_rate * winner_stats["food_eaten"] * 0.05
    return updated


def _finish_reason(turn: int, max_turns: int, board: Board) -> str:
    if not board.snakes:
        return "all_eliminated"
    if len(board.snakes) == 1:
        return "single_survivor"
    if turn >= max_turns:
        return "turn_limit"
    return "stopped"


def _coord_to_dict(coord: Coord) -> dict[str, int]:
    return {"x": coord.x, "y": coord.y}
