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
from battlesnake.game import Board, Coord, Snake
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
    "deepening_enabled": 1.0,
    "deepening_depth": 3.0,
    "deepening_top_candidates": 2.0,
    "deepening_interaction_radius": 6.0,
    "deepening_margin_ms": 20.0,
    "deepening_trap_penalty": 900_000.0,
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
                    deepening=_deepening_disabled_record(),
                    fallback_reason="all_candidates_gated",
                )
                return move, record

            candidate_scores = []
            scored = []
            candidates_by_move = {candidate.move: candidate for candidate in gates.candidates}
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

            deepening = _deepening_disabled_record()
            if _deepening_enabled(self.theta):
                depth1_scores = {detail["move"]: dict(detail) for detail in candidate_scores if detail.get("score") is not None}
                try:
                    deepened_scores, deepening = self._deepened_scores(
                        board=board,
                        snake_id=snake_id,
                        scored=scored,
                        candidates_by_move=candidates_by_move,
                        deadline=deadline,
                    )
                except _DeadlineExceeded:
                    deepened_scores = {}
                    deepening = _deepening_timeout_record(depth1_move=scored[0][2])

                if deepened_scores:
                    candidate_scores = _merge_deepened_scores(candidate_scores, deepened_scores)
                    scored = [
                        (
                            float(detail["score"]),
                            -MOVE_ORDER.index(str(detail["move"])),
                            str(detail["move"]),
                        )
                        for detail in candidate_scores
                        if detail.get("score") is not None
                    ]
                    scored.sort(reverse=True)
                else:
                    candidate_scores = _mark_depth1_fallback(candidate_scores, depth1_scores)

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
                deepening=deepening,
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
                "deepening": _deepening_timeout_record(depth1_move=None),
            }
            return move, record

    def _deepened_scores(
        self,
        *,
        board: Board,
        snake_id: str,
        scored: list[tuple[float, int, str]],
        candidates_by_move: dict[str, StandardMoveCandidate],
        deadline: float,
    ) -> tuple[dict[str, dict[str, Any]], dict[str, Any]]:
        depth = _deepening_depth(self.theta)
        top_count = _deepening_top_candidates(self.theta, len(scored))
        margin_s = max(0.0, self.theta["deepening_margin_ms"] / 1000.0)
        deep_deadline = deadline - margin_s
        if depth < 2 or top_count <= 0 or time.perf_counter() >= deep_deadline:
            raise _DeadlineExceeded

        top_moves = [move for _score, _rank, move in scored[:top_count]]
        results: dict[str, dict[str, Any]] = {}
        summaries: list[dict[str, Any]] = []
        refused_traps = 0
        for move in top_moves:
            _check_deadline(deep_deadline)
            candidate = candidates_by_move[move]
            value, detail = self._deep_search_root(board, snake_id, candidate, depth, deep_deadline)
            depth1_score = scored[[item[2] for item in scored].index(move)][0]
            refused = (
                value <= -self.theta["deepening_trap_penalty"]
                and detail["terminal_rate"] >= 1.0
                and not detail["active_opponents"]
            )
            final_score = value if refused else depth1_score
            if refused:
                refused_traps += 1
            result = {
                **detail,
                "status": "completed",
                "depth1_score": depth1_score,
                "score": final_score,
                "deep_score": value,
                "refused_trap": refused,
            }
            results[move] = result
            summaries.append({key: result[key] for key in (
                "move",
                "depth",
                "active_opponents",
                "frozen_opponents",
                "nodes",
                "terminal_rate",
                "deep_score",
                "depth1_score",
                "score",
                "refused_trap",
                "frozen_interaction_risk",
            )})

        return results, {
            "enabled": True,
            "status": "completed",
            "depth": depth,
            "top_candidates": top_moves,
            "refused_traps": refused_traps,
            "candidates": summaries,
        }

    def _deep_search_root(
        self,
        board: Board,
        snake_id: str,
        candidate: StandardMoveCandidate,
        depth: int,
        deadline: float,
    ) -> tuple[float, dict[str, Any]]:
        active = _active_opponents(board, snake_id, depth, self.theta)
        frozen = sorted(opponent_id for opponent_id in board.snakes if opponent_id != snake_id and opponent_id not in active)
        frozen_occupied = _occupied_by_snakes(board, frozen)
        nodes = 0
        root_target = _coord_from_tuple(candidate.target)
        if _coord_key(root_target) in frozen_occupied:
            return -1_000_000.0, _deep_candidate_record(
                candidate=candidate,
                depth=depth,
                active=active,
                frozen=frozen,
                nodes=1,
                score=-1_000_000.0,
                terminal_rate=1.0,
                frozen_risk=_frozen_interaction_risk(board, root_target, frozen, depth),
            )

        opponent_joint_moves = _joint_active_moves(board, active, frozen_occupied)
        if not opponent_joint_moves:
            opponent_joint_moves = [{}]

        worst = math.inf
        terminal_outcomes = 0
        outcome_count = 0
        for opponent_moves in opponent_joint_moves:
            _check_deadline(deadline)
            joint_moves = dict(opponent_moves)
            joint_moves[snake_id] = candidate.move
            next_board = _apply_dynamic_moves(board, joint_moves, frozen)
            nodes += 1
            value, child_nodes = self._deep_search_value(
                next_board,
                snake_id,
                active,
                frozen,
                depth_remaining=depth - 1,
                deadline=deadline,
            )
            nodes += child_nodes
            worst = min(worst, value)
            outcome_count += 1
            if value <= -self.theta["deepening_trap_penalty"]:
                terminal_outcomes += 1

        score = worst if worst < math.inf else -1_000_000.0
        return score, _deep_candidate_record(
            candidate=candidate,
            depth=depth,
            active=active,
            frozen=frozen,
            nodes=nodes,
            score=score,
            terminal_rate=terminal_outcomes / outcome_count if outcome_count else 1.0,
            frozen_risk=_frozen_interaction_risk(board, root_target, frozen, depth),
        )

    def _deep_search_value(
        self,
        board: Board,
        snake_id: str,
        active: list[str],
        frozen: list[str],
        *,
        depth_remaining: int,
        deadline: float,
    ) -> tuple[float, int]:
        _check_deadline(deadline)
        if snake_id not in board.snakes:
            return -1_000_000.0, 1
        if depth_remaining <= 0 or len(board.snakes) <= 1:
            return _deep_leaf_utility(board, snake_id, self.theta), 1

        active = [opponent_id for opponent_id in active if opponent_id in board.snakes]
        frozen_occupied = _occupied_by_snakes(board, frozen)
        own_moves = _safe_moves_with_frozen(board, snake_id, frozen_occupied)
        if not own_moves:
            return -1_000_000.0, 1

        best = -math.inf
        nodes = 1
        for own_move in own_moves:
            _check_deadline(deadline)
            worst = math.inf
            opponent_joint_moves = _joint_active_moves(board, active, frozen_occupied)
            if not opponent_joint_moves:
                opponent_joint_moves = [{}]
            for opponent_moves in opponent_joint_moves:
                joint_moves = dict(opponent_moves)
                joint_moves[snake_id] = own_move
                next_board = _apply_dynamic_moves(board, joint_moves, frozen)
                child_value, child_nodes = self._deep_search_value(
                    next_board,
                    snake_id,
                    active,
                    frozen,
                    depth_remaining=depth_remaining - 1,
                    deadline=deadline,
                )
                nodes += child_nodes
                worst = min(worst, child_value)
            best = max(best, worst)
        return best, nodes

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
    deepening: dict[str, Any],
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
        "deepening": deepening,
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


def _deepening_enabled(theta: dict[str, float]) -> bool:
    return theta.get("deepening_enabled", 0.0) > 0.0


def _deepening_depth(theta: dict[str, float]) -> int:
    return max(1, min(3, int(theta.get("deepening_depth", 1.0))))


def _deepening_top_candidates(theta: dict[str, float], scored_count: int) -> int:
    return max(0, min(scored_count, int(theta.get("deepening_top_candidates", 0.0))))


def _deepening_disabled_record() -> dict[str, Any]:
    return {"enabled": False, "status": "disabled"}


def _deepening_timeout_record(depth1_move: str | None) -> dict[str, Any]:
    return {
        "enabled": True,
        "status": "timeout_depth1_fallback",
        "depth1_move": depth1_move,
    }


def _merge_deepened_scores(
    candidate_scores: list[dict[str, Any]],
    deepened_scores: dict[str, dict[str, Any]],
) -> list[dict[str, Any]]:
    merged = []
    for detail in candidate_scores:
        move = detail.get("move")
        if move not in deepened_scores:
            merged.append(detail)
            continue
        deepened = deepened_scores[str(move)]
        updated = dict(detail)
        updated["depth1_score"] = detail["score"]
        updated["score"] = deepened["score"]
        updated["deepening"] = deepened
        merged.append(updated)
    return merged


def _mark_depth1_fallback(
    candidate_scores: list[dict[str, Any]],
    depth1_scores: dict[str, dict[str, Any]],
) -> list[dict[str, Any]]:
    marked = []
    for detail in candidate_scores:
        move = detail.get("move")
        if move in depth1_scores:
            updated = dict(detail)
            updated["depth1_score"] = detail["score"]
            updated["deepening"] = {"status": "depth1_fallback"}
            marked.append(updated)
        else:
            marked.append(detail)
    return marked


def _deep_candidate_record(
    *,
    candidate: StandardMoveCandidate,
    depth: int,
    active: list[str],
    frozen: list[str],
    nodes: int,
    score: float,
    terminal_rate: float,
    frozen_risk: dict[str, Any],
) -> dict[str, Any]:
    return {
        "move": candidate.move,
        "depth": depth,
        "active_opponents": active,
        "frozen_opponents": frozen,
        "nodes": nodes,
        "terminal_rate": terminal_rate,
        "deep_score": score,
        "frozen_interaction_risk": frozen_risk,
    }


def _active_opponents(board: Board, snake_id: str, depth: int, theta: dict[str, float]) -> list[str]:
    radius = theta.get("deepening_interaction_radius", 0.0)
    if radius <= 0:
        radius = 2 * depth
    own_head = board.head(snake_id)
    active = []
    for opponent_id in sorted(board.snakes):
        if opponent_id == snake_id:
            continue
        if _manhattan(own_head, board.head(opponent_id)) <= radius:
            active.append(opponent_id)
    return active


def _occupied_by_snakes(board: Board, snake_ids: Iterable[str]) -> set[tuple[int, int]]:
    occupied: set[tuple[int, int]] = set()
    for snake_id in snake_ids:
        snake = board.snakes.get(snake_id)
        if snake is None:
            continue
        occupied.update(_coord_key(coord) for coord in snake.body)
    return occupied


def _safe_moves_with_frozen(board: Board, snake_id: str, frozen_occupied: set[tuple[int, int]]) -> list[str]:
    if snake_id not in board.snakes:
        return []
    head = board.head(snake_id)
    moves = []
    for move in board.safe_moves(snake_id):
        target = board.step(head, move)
        if _coord_key(target) not in frozen_occupied:
            moves.append(str(move))
    return moves


def _joint_active_moves(
    board: Board,
    active: list[str],
    frozen_occupied: set[tuple[int, int]],
) -> list[dict[str, str]]:
    move_lists: list[list[str | None]] = []
    active_ids: list[str] = []
    for opponent_id in active:
        if opponent_id not in board.snakes:
            continue
        moves = _safe_moves_with_frozen(board, opponent_id, frozen_occupied)
        move_lists.append(moves if moves else [None])
        active_ids.append(opponent_id)
    if not active_ids:
        return [{}]

    joints = []
    for product in itertools.product(*move_lists):
        moves = {
            opponent_id: move
            for opponent_id, move in zip(active_ids, product)
            if move is not None
        }
        joints.append(moves)
    return joints


def _apply_dynamic_moves(board: Board, joint_moves: dict[str, str], frozen: list[str]) -> Board:
    frozen_set = set(frozen)
    frozen_occupied = _occupied_by_snakes(board, frozen)
    dynamic_snakes = [
        snake
        for snake_id, snake in board.snakes.items()
        if snake_id not in frozen_set
    ]
    dynamic_board = Board(
        board.width,
        board.height,
        dynamic_snakes,
        food=board.food,
        hazards=board.hazards,
        ruleset_name=board.ruleset_name,
        hazard_damage=board.hazard_damage,
    )
    filtered_moves = {}
    for snake_id, move in joint_moves.items():
        if snake_id not in dynamic_board.snakes:
            continue
        target = dynamic_board.step(dynamic_board.head(snake_id), move)
        if _coord_key(target) in frozen_occupied:
            continue
        filtered_moves[snake_id] = move

    next_dynamic = dynamic_board.clone_and_apply(filtered_moves)
    snakes: list[Snake] = list(next_dynamic.snakes.values())
    snakes.extend(board.snakes[snake_id] for snake_id in frozen if snake_id in board.snakes)
    return Board(
        board.width,
        board.height,
        snakes,
        food=next_dynamic.food,
        hazards=board.hazards,
        ruleset_name=board.ruleset_name,
        hazard_damage=board.hazard_damage,
    )


def _deep_leaf_utility(board: Board, snake_id: str, theta: dict[str, float]) -> float:
    if snake_id not in board.snakes:
        return -1_000_000.0
    if len(board.snakes) == 1:
        return 1_000_000.0
    return (
        evaluate(board, snake_id, _evaluation_theta(theta))
        + _space_adjustment(board, snake_id, theta)
        + _pocket_adjustment(board, snake_id, theta)
    )


def _frozen_interaction_risk(
    board: Board,
    root_target: Coord,
    frozen: list[str],
    depth: int,
) -> dict[str, Any]:
    risk_radius = max(0, 2 * (depth - 1))
    risky = [
        snake_id
        for snake_id in frozen
        if snake_id in board.snakes and _manhattan(root_target, board.head(snake_id)) <= risk_radius
    ]
    return {
        "checked": len(frozen),
        "risk_radius": risk_radius,
        "risky": risky,
    }


def _check_deadline(deadline: float) -> None:
    if time.perf_counter() > deadline:
        raise _DeadlineExceeded
