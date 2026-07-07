"""Deterministic hard gates for the Standard FFA development strategy."""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum
from typing import Any

from battlesnake.battlesnake_native import reachable_space
from battlesnake.game import Board, Coord, Snake
from battlesnake.types import Move


MOVE_ORDER: tuple[str, ...] = (
    Move.UP.value,
    Move.DOWN.value,
    Move.LEFT.value,
    Move.RIGHT.value,
)


class DeathClass(str, Enum):
    """Immediate or near-immediate Standard FFA hard-gate classes."""

    WALL = "wall"
    BODY = "body"
    SELF = "self"
    HEAD_TO_HEAD_LOSING = "head_to_head_losing"
    HAZARD_STARVATION = "hazard_starvation"
    TRAPPED_NEXT_TURN = "trapped_next_turn"


DEATH_CLASS_SEVERITY: dict[DeathClass, int] = {
    DeathClass.TRAPPED_NEXT_TURN: 10,
    DeathClass.HEAD_TO_HEAD_LOSING: 20,
    DeathClass.HAZARD_STARVATION: 30,
    DeathClass.BODY: 40,
    DeathClass.SELF: 40,
    DeathClass.WALL: 50,
}


@dataclass(frozen=True)
class StandardMoveCandidate:
    """Serializable classification for one root candidate move."""

    move: str
    target: tuple[int, int]
    in_bounds: bool
    safe_by_board_rules: bool
    enters_hazard: bool
    candidate_food: bool
    candidate_occupied: bool
    immediate_safe_move_count_after: int
    immediate_reachable_space: int
    death_class: DeathClass | None
    terminal: bool
    severe: bool
    eligible: bool
    fallback_rank: tuple[int, int, int, int]

    def to_dict(self) -> dict[str, Any]:
        """Return a JSON-serializable telemetry record."""

        return {
            "move": self.move,
            "target": {"x": self.target[0], "y": self.target[1]},
            "in_bounds": self.in_bounds,
            "safe_by_board_rules": self.safe_by_board_rules,
            "enters_hazard": self.enters_hazard,
            "candidate_food": self.candidate_food,
            "candidate_occupied": self.candidate_occupied,
            "immediate_safe_move_count_after": self.immediate_safe_move_count_after,
            "immediate_reachable_space": self.immediate_reachable_space,
            "death_class": self.death_class.value if self.death_class is not None else None,
            "terminal": self.terminal,
            "severe": self.severe,
            "eligible": self.eligible,
            "fallback_rank": list(self.fallback_rank),
        }


@dataclass(frozen=True)
class StandardGateResult:
    """Complete hard-gate result for a Standard FFA root position."""

    candidates: tuple[StandardMoveCandidate, ...]

    @property
    def eligible_candidates(self) -> tuple[StandardMoveCandidate, ...]:
        return tuple(candidate for candidate in self.candidates if candidate.eligible)

    @property
    def least_bad_candidate(self) -> StandardMoveCandidate:
        return min(self.candidates, key=lambda candidate: candidate.fallback_rank)

    def to_dict(self) -> dict[str, Any]:
        """Return JSON-serializable telemetry for all candidates."""

        return {
            "candidates": [candidate.to_dict() for candidate in self.candidates],
            "eligible_moves": [candidate.move for candidate in self.eligible_candidates],
            "least_bad_move": self.least_bad_candidate.move,
        }


def classify_standard_ffa_candidates(board: Board, snake_id: str) -> StandardGateResult:
    """Classify all four Standard FFA candidate moves for hard-gate decisions."""

    snake = board.snakes[snake_id]
    head = board.head(snake_id)
    candidates = tuple(
        _classify_candidate(board, snake, snake_id, head, move, move_index)
        for move_index, move in enumerate(MOVE_ORDER)
    )
    return StandardGateResult(candidates=candidates)


def _classify_candidate(
    board: Board,
    snake: Snake,
    snake_id: str,
    head: Coord,
    move: str,
    move_index: int,
) -> StandardMoveCandidate:
    target_coord = board.step(head, move)
    target = _coord_key(target_coord)
    in_bounds = board.in_bounds(target_coord)
    occupied = target in _coord_keys(board.occupied(include_tails=True))
    food = target in _coord_keys(board.food)
    hazard = target in _coord_keys(board.hazards)
    safe_by_board_rules = in_bounds and board.is_safe(target_coord, snake_id)
    hazard_starvation = hazard and not food and snake.health <= board.hazard_damage + 1

    immediate_safe_count = 0
    immediate_space = 0
    if in_bounds and safe_by_board_rules and not hazard_starvation:
        after_board = _board_after_our_move(board, snake_id, target_coord, food, hazard)
        if snake_id in after_board.snakes:
            after_head = after_board.head(snake_id)
            immediate_safe_count = len(after_board.safe_moves(snake_id))
            immediate_space = reachable_space(after_board, after_head, snake_id)

    death_class = _death_class_for_candidate(
        board=board,
        snake=snake,
        snake_id=snake_id,
        target=target,
        in_bounds=in_bounds,
        safe_by_board_rules=safe_by_board_rules,
        hazard_starvation=hazard_starvation,
        immediate_safe_count=immediate_safe_count,
        immediate_space=immediate_space,
    )
    terminal = death_class in {
        DeathClass.WALL,
        DeathClass.BODY,
        DeathClass.SELF,
        DeathClass.HEAD_TO_HEAD_LOSING,
        DeathClass.HAZARD_STARVATION,
    }
    severe = death_class == DeathClass.TRAPPED_NEXT_TURN
    eligible = safe_by_board_rules and not terminal and not severe
    severity = DEATH_CLASS_SEVERITY.get(death_class, 0) if death_class is not None else 0

    return StandardMoveCandidate(
        move=move,
        target=target,
        in_bounds=in_bounds,
        safe_by_board_rules=safe_by_board_rules,
        enters_hazard=hazard,
        candidate_food=food,
        candidate_occupied=occupied,
        immediate_safe_move_count_after=immediate_safe_count,
        immediate_reachable_space=immediate_space,
        death_class=death_class,
        terminal=terminal,
        severe=severe,
        eligible=eligible,
        fallback_rank=(severity, -immediate_space, -immediate_safe_count, move_index),
    )


def _death_class_for_candidate(
    *,
    board: Board,
    snake: Snake,
    snake_id: str,
    target: tuple[int, int],
    in_bounds: bool,
    safe_by_board_rules: bool,
    hazard_starvation: bool,
    immediate_safe_count: int,
    immediate_space: int,
) -> DeathClass | None:
    if not in_bounds:
        return DeathClass.WALL
    if hazard_starvation:
        return DeathClass.HAZARD_STARVATION
    if not safe_by_board_rules:
        if _coord_in_snake_body(target, board.snakes[snake_id]):
            return DeathClass.SELF
        if _loses_head_to_head(board, snake, snake_id, target):
            return DeathClass.HEAD_TO_HEAD_LOSING
        if _coord_in_any_opponent_body(target, board, snake_id):
            return DeathClass.BODY
        return DeathClass.BODY
    if immediate_space <= 0 or immediate_safe_count <= 0:
        return DeathClass.TRAPPED_NEXT_TURN
    return None


def _board_after_our_move(
    board: Board,
    snake_id: str,
    target: Coord,
    candidate_food: bool,
    candidate_hazard: bool,
) -> Board:
    snakes: list[Snake] = []
    for current_id, current_snake in board.snakes.items():
        if current_id == snake_id:
            body = [target]
            if candidate_food:
                body.extend(current_snake.body)
            else:
                body.extend(current_snake.body[:-1])

            health = current_snake.health - 1
            if candidate_hazard:
                health -= board.hazard_damage
            if candidate_food:
                health = 100

            snakes.append(
                Snake(
                    id=current_snake.id,
                    name=current_snake.name,
                    health=health,
                    body=body,
                    length=len(body),
                )
            )
            continue

        body = list(current_snake.body)
        if body and _coord_key(body[-1]) == _coord_key(target) and _opponent_tail_vacates(board, current_snake):
            body = body[:-1]
        snakes.append(
            Snake(
                id=current_snake.id,
                name=current_snake.name,
                health=current_snake.health,
                body=body,
                length=len(body),
            )
        )

    remaining_food = [coord for coord in board.food if _coord_key(coord) != _coord_key(target)]
    return Board(
        board.width,
        board.height,
        snakes,
        food=remaining_food,
        hazards=board.hazards,
        ruleset_name=board.ruleset_name,
        hazard_damage=board.hazard_damage,
    )


def _opponent_tail_vacates(board: Board, snake: Snake) -> bool:
    if board.ruleset_name == "constrictor" or not snake.body:
        return False
    head = snake.head
    if head is None:
        return False
    food = _coord_keys(board.food)
    return all(_coord_key(board.step(head, move)) not in food for move in MOVE_ORDER)


def _loses_head_to_head(board: Board, snake: Snake, snake_id: str, target: tuple[int, int]) -> bool:
    own_length = _snake_length(snake)
    for opponent_id, opponent in board.snakes.items():
        if opponent_id == snake_id or opponent.head is None or _snake_length(opponent) < own_length:
            continue
        if _manhattan(_coord_key(opponent.head), target) == 1:
            return True
    return False


def _coord_in_snake_body(target: tuple[int, int], snake: Snake) -> bool:
    return target in {_coord_key(coord) for coord in snake.body}


def _coord_in_any_opponent_body(target: tuple[int, int], board: Board, snake_id: str) -> bool:
    for opponent_id, opponent in board.snakes.items():
        if opponent_id != snake_id and _coord_in_snake_body(target, opponent):
            return True
    return False


def _snake_length(snake: Snake) -> int:
    return snake.length or len(snake.body)


def _coord_key(coord: Coord) -> tuple[int, int]:
    return (coord.x, coord.y)


def _coord_keys(coords: set[Coord]) -> set[tuple[int, int]]:
    return {_coord_key(coord) for coord in coords}


def _manhattan(left: tuple[int, int], right: tuple[int, int]) -> int:
    return abs(left[0] - right[0]) + abs(left[1] - right[1])
