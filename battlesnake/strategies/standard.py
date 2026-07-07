"""Deterministic Standard free-for-all strategy."""

from __future__ import annotations

import itertools
import math
import time
from dataclasses import dataclass
from typing import Iterable

from battlesnake.core.astar import shortest_path
from battlesnake.core.evaluation import WEIGHTS as EVALUATION_WEIGHTS
from battlesnake.core.evaluation import evaluate
from battlesnake.core.flood_fill import reachable_space
from battlesnake.game import Board, Coord
from battlesnake.strategies.base import Strategy
from battlesnake.strategies.first_safe import StrategyFirstSafe
from battlesnake.strategies.standard_gates import (
    MOVE_ORDER,
    StandardMoveCandidate,
    classify_standard_ffa_candidates,
)
from battlesnake.types import Move


DEFAULT_STANDARD_THETA: dict[str, float] = {
    **EVALUATION_WEIGHTS,
    "opponent_reachable_space": 0.35,
    "territory_delta": 1.4,
    "opponent_safe_moves": 18.0,
    "w_expected": 1.0,
    "w_worst": 0.38,
    "w_space_log": 80.0,
    "w_space_ratio": 18.0,
    "w_escape": 55.0,
    "w_zero_escape": 650.0,
    "w_losing_h2h": 900_000.0,
    "w_winning_h2h": 120.0,
    "w_food_on_cell": 180.0,
    "w_food_route": 160.0,
    "w_contested_food": 550.0,
    "w_pocket": 420.0,
    "food_urgency_health": 35.0,
    "pocket_space_per_length": 2.5,
    "nearby_opponent_distance": 4.0,
}

_EVALUATOR_WEIGHT_KEYS = set(EVALUATION_WEIGHTS)


@dataclass(frozen=True)
class _Scenario:
    moves: dict[str, str]
    probability: float


class _DeadlineExceeded(Exception):
    pass


class StrategyStandard(Strategy):
    """Risk-aware deterministic strategy for standard multi-snake FFA games."""

    def __init__(
        self,
        *,
        theta: dict[str, float] | None = None,
        max_scenarios: int = 12,
        deadline_ms: int = 80,
    ) -> None:
        self.theta = dict(DEFAULT_STANDARD_THETA)
        if theta:
            self.theta.update(theta)
        self.max_scenarios = max(1, max_scenarios)
        self.deadline_ms = max(1, deadline_ms)
        self._fallback = StrategyFirstSafe()

    def decide(self, board: Board, snake_id: str) -> Move | str:
        """Choose a Standard FFA move using deterministic root scenario scoring."""

        deadline = time.perf_counter() + self.deadline_ms / 1000.0
        try:
            gates = classify_standard_ffa_candidates(board, snake_id)
            candidates = gates.eligible_candidates
            if not candidates:
                return gates.least_bad_candidate.move

            priors = _opponent_priors(board, snake_id)
            scored = [
                (
                    self._score_candidate(board, snake_id, candidate, priors, deadline),
                    -MOVE_ORDER.index(candidate.move),
                    candidate.move,
                )
                for candidate in candidates
            ]
            scored.sort(reverse=True)
            return scored[0][2]
        except _DeadlineExceeded:
            return self._fallback.decide(board, snake_id)

    def _score_candidate(
        self,
        board: Board,
        snake_id: str,
        candidate: StandardMoveCandidate,
        priors: dict[str, list[tuple[str, float]]],
        deadline: float,
    ) -> float:
        scenarios = _scenario_set(board, snake_id, candidate, priors, self.max_scenarios, self.theta)
        weighted_total = 0.0
        probability_total = 0.0
        worst = math.inf

        for scenario in scenarios:
            _check_deadline(deadline)
            utility = self._scenario_utility(board, snake_id, candidate, scenario)
            weighted_total += scenario.probability * utility
            probability_total += scenario.probability
            worst = min(worst, utility)

        expected = weighted_total / probability_total if probability_total > 0 else worst
        return self.theta["w_expected"] * expected + self.theta["w_worst"] * worst

    def _scenario_utility(
        self,
        board: Board,
        snake_id: str,
        candidate: StandardMoveCandidate,
        scenario: _Scenario,
    ) -> float:
        joint_moves = dict(scenario.moves)
        joint_moves[snake_id] = candidate.move
        next_board = board.clone_and_apply(joint_moves)
        if snake_id not in next_board.snakes:
            return -1_000_000.0

        score = evaluate(next_board, snake_id, _evaluation_theta(self.theta))
        score += _space_adjustment(next_board, snake_id, self.theta)
        score += _head_pressure_adjustment(board, snake_id, candidate, scenario, self.theta)
        score += _food_adjustment(board, next_board, snake_id, candidate, self.theta)
        score += _pocket_adjustment(next_board, snake_id, self.theta)
        return score


def _opponent_priors(board: Board, snake_id: str) -> dict[str, list[tuple[str, float]]]:
    priors: dict[str, list[tuple[str, float]]] = {}
    for opponent_id in sorted(board.snakes):
        if opponent_id == snake_id:
            continue
        safe_moves = [move for move in MOVE_ORDER if move in set(board.safe_moves(opponent_id))]
        moves = safe_moves or list(MOVE_ORDER)
        probability = 1.0 / len(moves)
        priors[opponent_id] = [(move, probability) for move in moves]
    return priors


def _scenario_set(
    board: Board,
    snake_id: str,
    candidate: StandardMoveCandidate,
    priors: dict[str, list[tuple[str, float]]],
    max_scenarios: int,
    theta: dict[str, float],
) -> list[_Scenario]:
    scenarios = _top_joint_scenarios(priors, min(8, max_scenarios))
    scenarios.extend(_forced_head_to_head_scenarios(board, snake_id, candidate, priors))
    scenarios.extend(_nearby_worst_case_scenarios(board, snake_id, candidate, priors, theta))
    return _dedupe_scenarios(scenarios)[:max_scenarios]


def _top_joint_scenarios(priors: dict[str, list[tuple[str, float]]], limit: int) -> list[_Scenario]:
    if not priors:
        return [_Scenario({}, 1.0)]

    opponent_ids = sorted(priors)
    rows = []
    for product in itertools.product(*(priors[opponent_id] for opponent_id in opponent_ids)):
        moves = {opponent_id: move for opponent_id, (move, _prob) in zip(opponent_ids, product)}
        probability = math.prod(probability for _move, probability in product)
        rows.append(_Scenario(moves, probability))
    rows.sort(key=lambda scenario: (-scenario.probability, tuple(scenario.moves[opponent] for opponent in opponent_ids)))
    return rows[:limit]


def _forced_head_to_head_scenarios(
    board: Board,
    snake_id: str,
    candidate: StandardMoveCandidate,
    priors: dict[str, list[tuple[str, float]]],
) -> list[_Scenario]:
    scenarios: list[_Scenario] = []
    own_length = _snake_length(board, snake_id)
    target = candidate.target
    for opponent_id in sorted(priors):
        opponent = board.snakes[opponent_id]
        if _snake_length(board, opponent_id) < own_length or opponent.head is None:
            continue
        move = _move_to_target(board, opponent_id, target)
        if move is None:
            continue
        moves = _default_opponent_moves(priors)
        moves[opponent_id] = move
        scenarios.append(_Scenario(moves, _scenario_probability(moves, priors)))
    return scenarios


def _nearby_worst_case_scenarios(
    board: Board,
    snake_id: str,
    candidate: StandardMoveCandidate,
    priors: dict[str, list[tuple[str, float]]],
    theta: dict[str, float],
) -> list[_Scenario]:
    scenarios: list[_Scenario] = []
    our_head = board.head(snake_id)
    for opponent_id in sorted(priors):
        opponent_head = board.head(opponent_id)
        if _manhattan(our_head, opponent_head) > theta["nearby_opponent_distance"]:
            continue
        moves = _default_opponent_moves(priors)
        moves[opponent_id] = min(
            (move for move, _probability in priors[opponent_id]),
            key=lambda move: (_manhattan(board.step(opponent_head, move), _coord_from_tuple(candidate.target)), MOVE_ORDER.index(move)),
        )
        scenarios.append(_Scenario(moves, _scenario_probability(moves, priors)))
    return scenarios


def _dedupe_scenarios(scenarios: Iterable[_Scenario]) -> list[_Scenario]:
    deduped: dict[tuple[tuple[str, str], ...], _Scenario] = {}
    for scenario in scenarios:
        key = tuple(sorted(scenario.moves.items()))
        if key not in deduped or scenario.probability > deduped[key].probability:
            deduped[key] = scenario
    return sorted(deduped.values(), key=lambda scenario: (-scenario.probability, tuple(sorted(scenario.moves.items()))))


def _default_opponent_moves(priors: dict[str, list[tuple[str, float]]]) -> dict[str, str]:
    return {opponent_id: moves[0][0] for opponent_id, moves in priors.items()}


def _scenario_probability(moves: dict[str, str], priors: dict[str, list[tuple[str, float]]]) -> float:
    probability = 1.0
    for opponent_id, move in moves.items():
        prior = dict(priors[opponent_id])
        probability *= prior.get(move, 0.0)
    return probability or 1e-6


def _space_adjustment(board: Board, snake_id: str, theta: dict[str, float]) -> float:
    head = board.head(snake_id)
    space = reachable_space(board, head, snake_id)
    safe_count = len(board.safe_moves(snake_id))
    length = max(_snake_length(board, snake_id), 1)
    score = theta["w_space_log"] * math.log1p(space)
    score += theta["w_space_ratio"] * (space / length)
    score += theta["w_escape"] * safe_count
    if safe_count == 0:
        score -= theta["w_zero_escape"]
    return score


def _head_pressure_adjustment(
    board: Board,
    snake_id: str,
    candidate: StandardMoveCandidate,
    scenario: _Scenario,
    theta: dict[str, float],
) -> float:
    own_length = _snake_length(board, snake_id)
    score = 0.0
    for opponent_id, move in scenario.moves.items():
        if _coord_key(board.step(board.head(opponent_id), move)) != candidate.target:
            continue
        if _snake_length(board, opponent_id) >= own_length:
            score -= theta["w_losing_h2h"]
        else:
            score += min(theta["w_winning_h2h"], theta["w_winning_h2h"] * 0.5)
    return score


def _food_adjustment(
    board: Board,
    next_board: Board,
    snake_id: str,
    candidate: StandardMoveCandidate,
    theta: dict[str, float],
) -> float:
    if not candidate.candidate_food:
        return 0.0

    snake = board.snakes[snake_id]
    hunger = max(0.0, min(1.0, (theta["food_urgency_health"] - snake.health) / theta["food_urgency_health"]))
    target = _coord_from_tuple(candidate.target)
    path = shortest_path(board, board.head(snake_id), target, snake_id)
    route_distance = max(len(path) - 1, 1) if path else 8
    score = theta["w_food_on_cell"] * (1.0 + hunger)
    score += hunger * theta["w_food_route"] / route_distance

    escaped_after_eating = snake_id in next_board.snakes and len(next_board.safe_moves(snake_id)) > 0
    if not escaped_after_eating or not _wins_food_race(board, snake_id, target):
        score -= theta["w_contested_food"]
    return score


def _pocket_adjustment(board: Board, snake_id: str, theta: dict[str, float]) -> float:
    head = board.head(snake_id)
    space = reachable_space(board, head, snake_id)
    length = max(_snake_length(board, snake_id), 1)
    threshold = theta["pocket_space_per_length"] * length
    if space >= threshold:
        return 0.0
    return -theta["w_pocket"] * ((threshold - space) / threshold)


def _wins_food_race(board: Board, snake_id: str, food: Coord) -> bool:
    my_distance = _path_distance(board, snake_id, food)
    if my_distance is None:
        return False
    for opponent_id in board.snakes:
        if opponent_id == snake_id:
            continue
        opponent_distance = _path_distance(board, opponent_id, food)
        if opponent_distance is not None and opponent_distance <= my_distance:
            return False
    return True


def _path_distance(board: Board, snake_id: str, target: Coord) -> int | None:
    path = shortest_path(board, board.head(snake_id), target, snake_id)
    if not path:
        return None
    return max(0, len(path) - 1)


def _move_to_target(board: Board, snake_id: str, target: tuple[int, int]) -> str | None:
    head = board.head(snake_id)
    for move in MOVE_ORDER:
        if _coord_key(board.step(head, move)) == target:
            return move
    return None


def _evaluation_theta(theta: dict[str, float]) -> dict[str, float]:
    return {key: theta[key] for key in _EVALUATOR_WEIGHT_KEYS}


def _snake_length(board: Board, snake_id: str) -> int:
    snake = board.snakes[snake_id]
    return snake.length or len(snake.body)


def _coord_key(coord: Coord) -> tuple[int, int]:
    return (coord.x, coord.y)


def _coord_from_tuple(value: tuple[int, int]) -> Coord:
    return Coord(value[0], value[1])


def _manhattan(left: Coord, right: Coord) -> int:
    return abs(left.x - right.x) + abs(left.y - right.y)


def _check_deadline(deadline: float) -> None:
    if time.perf_counter() > deadline:
        raise _DeadlineExceeded
