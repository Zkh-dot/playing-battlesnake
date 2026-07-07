"""Deterministic Standard free-for-all strategy."""

from __future__ import annotations

import itertools
import math
import time
from dataclasses import dataclass
from typing import Any, Iterable

from battlesnake.core.astar import shortest_path
from battlesnake.core.evaluation import WEIGHTS as EVALUATION_WEIGHTS
from battlesnake.core.evaluation import evaluate
from battlesnake.core.flood_fill import reachable_space
from battlesnake.game import Board, Coord
from battlesnake.opponent_model_prior import LightGBMOpponentPrior, uniform_safe_priors
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
        opponent_prior: str = "uniform",
    ) -> None:
        self.theta = dict(DEFAULT_STANDARD_THETA)
        if theta:
            self.theta.update(theta)
        self.max_scenarios = max(1, max_scenarios)
        self.deadline_ms = max(1, deadline_ms)
        self.opponent_prior = opponent_prior
        self.current_turn = 0
        self._model_prior = LightGBMOpponentPrior() if opponent_prior == "model" else None
        self._fallback = StrategyFirstSafe()
        self.last_decision_record: dict[str, Any] | None = None

    def set_context(self, *, turn: int) -> None:
        self.current_turn = turn

    def decide(self, board: Board, snake_id: str) -> Move | str:
        """Choose a Standard FFA move using deterministic root scenario scoring."""

        move, record = self.explain_decision(board, snake_id)
        self.last_decision_record = record
        return move

    def explain_decision(self, board: Board, snake_id: str) -> tuple[Move | str, dict[str, Any]]:
        """Choose a move and return the complete decision telemetry record."""

        started = time.perf_counter()
        deadline = time.perf_counter() + self.deadline_ms / 1000.0
        try:
            gates = classify_standard_ffa_candidates(board, snake_id)
            candidates = gates.eligible_candidates
            priors = self._opponent_priors(board, snake_id)
            if not candidates:
                move = gates.least_bad_candidate.move
                record = _decision_record(
                    board=board,
                    snake_id=snake_id,
                    chosen_move=move,
                    gates=gates.to_dict(),
                    opponent_priors=priors,
                    opponent_prior_status=self._opponent_prior_status(),
                    candidate_scores=[
                        _unscored_candidate_record(candidate, reason="all_candidates_gated")
                        for candidate in gates.candidates
                    ],
                    started=started,
                    fallback_reason="all_candidates_gated",
                )
                return move, record

            candidate_scores = []
            scored = []
            for candidate in gates.candidates:
                if not candidate.eligible:
                    candidate_scores.append(_unscored_candidate_record(candidate, reason="gated"))
                    continue
                detail = self._score_candidate_detail(board, snake_id, candidate, priors, deadline)
                candidate_scores.append(detail)
                scored.append(
                    (
                        detail["score"],
                        -MOVE_ORDER.index(candidate.move),
                        candidate.move,
                    )
                )
            scored.sort(reverse=True)
            move = scored[0][2]
            record = _decision_record(
                board=board,
                snake_id=snake_id,
                chosen_move=move,
                gates=gates.to_dict(),
                opponent_priors=priors,
                opponent_prior_status=self._opponent_prior_status(),
                candidate_scores=candidate_scores,
                started=started,
            )
            return move, record
        except _DeadlineExceeded:
            move = self._fallback.decide(board, snake_id)
            record = {
                "strategy": "standard-v1",
                "snake_id": snake_id,
                "chosen_move": str(move),
                "fallback_reason": "deadline_exceeded",
                "latency_ms": _elapsed_ms(started),
                "candidates": [],
                "opponent_priors": {},
                "opponent_prior_status": self._opponent_prior_status(),
            }
            return move, record

    def _opponent_priors(self, board: Board, snake_id: str) -> dict[str, list[tuple[str, float]]]:
        if self._model_prior is None:
            return uniform_safe_priors(board, snake_id)
        return self._model_prior.priors(board, snake_id, turn=self.current_turn)

    def _opponent_prior_status(self) -> dict[str, Any]:
        if self._model_prior is None:
            return {"source": "uniform", "status": "uniform", "latency_ms": 0.0}
        return {
            "source": "model",
            "status": self._model_prior.last_status,
            "latency_ms": self._model_prior.last_latency_ms,
        }

    def _score_candidate(
        self,
        board: Board,
        snake_id: str,
        candidate: StandardMoveCandidate,
        priors: dict[str, list[tuple[str, float]]],
        deadline: float,
    ) -> float:
        return float(self._score_candidate_detail(board, snake_id, candidate, priors, deadline)["score"])

    def _score_candidate_detail(
        self,
        board: Board,
        snake_id: str,
        candidate: StandardMoveCandidate,
        priors: dict[str, list[tuple[str, float]]],
        deadline: float,
    ) -> dict[str, Any]:
        scenarios = _scenario_set(board, snake_id, candidate, priors, self.max_scenarios, self.theta)
        weighted_total = 0.0
        probability_total = 0.0
        worst = math.inf
        weighted_terms: dict[str, float] = {}

        for scenario in scenarios:
            _check_deadline(deadline)
            utility, terms = self._scenario_utility_terms(board, snake_id, candidate, scenario)
            weighted_total += scenario.probability * utility
            probability_total += scenario.probability
            worst = min(worst, utility)
            for key, value in terms.items():
                weighted_terms[key] = weighted_terms.get(key, 0.0) + scenario.probability * value

        expected = weighted_total / probability_total if probability_total > 0 else worst
        expected_terms = {
            key: value / probability_total
            for key, value in weighted_terms.items()
        } if probability_total > 0 else weighted_terms
        score = self.theta["w_expected"] * expected + self.theta["w_worst"] * worst
        return {
            **candidate.to_dict(),
            "score": score,
            "expected": expected,
            "worst": worst,
            "scenario_count": len(scenarios),
            "terms": expected_terms,
        }

    def _scenario_utility(
        self,
        board: Board,
        snake_id: str,
        candidate: StandardMoveCandidate,
        scenario: _Scenario,
    ) -> float:
        score, _terms = self._scenario_utility_terms(board, snake_id, candidate, scenario)
        return score

    def _scenario_utility_terms(
        self,
        board: Board,
        snake_id: str,
        candidate: StandardMoveCandidate,
        scenario: _Scenario,
    ) -> tuple[float, dict[str, float]]:
        joint_moves = dict(scenario.moves)
        joint_moves[snake_id] = candidate.move
        next_board = board.clone_and_apply(joint_moves)
        if snake_id not in next_board.snakes:
            return -1_000_000.0, {
                "terminal": -1_000_000.0,
                "native_evaluate": 0.0,
                "space": 0.0,
                "head_pressure": 0.0,
                "food": 0.0,
                "pocket": 0.0,
            }

        terms = {
            "terminal": 0.0,
            "native_evaluate": evaluate(next_board, snake_id, _evaluation_theta(self.theta)),
            "space": _space_adjustment(next_board, snake_id, self.theta),
            "head_pressure": _head_pressure_adjustment(board, snake_id, candidate, scenario, self.theta),
            "food": _food_adjustment(board, next_board, snake_id, candidate, self.theta),
            "pocket": _pocket_adjustment(next_board, snake_id, self.theta),
        }
        return sum(terms.values()), terms


def _decision_record(
    *,
    board: Board,
    snake_id: str,
    chosen_move: str,
    gates: dict[str, Any],
    opponent_priors: dict[str, list[tuple[str, float]]],
    opponent_prior_status: dict[str, Any],
    candidate_scores: list[dict[str, Any]],
    started: float,
    fallback_reason: str | None = None,
) -> dict[str, Any]:
    snake = board.snakes[snake_id]
    record = {
        "strategy": "standard-v1",
        "snake_id": snake_id,
        "chosen_move": chosen_move,
        "latency_ms": _elapsed_ms(started),
        "fallback_reason": fallback_reason,
        "snake": {
            "health": snake.health,
            "length": snake.length or len(snake.body),
            "head": _coord_dict(board.head(snake_id)),
        },
        "gates": gates,
        "opponent_prior_status": opponent_prior_status,
        "candidates": candidate_scores,
        "opponent_priors": {
            opponent_id: [
                {"move": move, "probability": probability}
                for move, probability in moves
            ]
            for opponent_id, moves in opponent_priors.items()
        },
    }
    return record


def _unscored_candidate_record(candidate: StandardMoveCandidate, *, reason: str) -> dict[str, Any]:
    return {
        **candidate.to_dict(),
        "score": None,
        "expected": None,
        "worst": None,
        "scenario_count": 0,
        "terms": {},
        "not_scored_reason": reason,
    }


def _elapsed_ms(started: float) -> float:
    return (time.perf_counter() - started) * 1000.0


def _coord_dict(coord: Coord) -> dict[str, int]:
    return {"x": coord.x, "y": coord.y}


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
