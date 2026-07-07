#!/usr/bin/env python3
"""Run paired Standard FFA arena comparisons."""

from __future__ import annotations

import argparse
import json
import math
import sys
import time
from collections import Counter
from pathlib import Path
from random import Random
from statistics import mean, median
from typing import Any

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from battlesnake.game import Board, Coord, Snake
from battlesnake.strategies.first_safe import StrategyFirstSafe
from battlesnake.strategies.standard import StrategyStandard


PLACEMENT_POINTS = {1: 1.0, 2: 0.55, 3: 0.20, 4: 0.0}
DEATH_PENALTY = {
    "wall": 1.0,
    "body": 0.8,
    "self": 0.8,
    "head_to_head": 0.7,
    "starvation": 0.6,
    "unknown": 0.5,
    "alive": 0.0,
    "won": 0.0,
}


def run_paired_arena(args: argparse.Namespace) -> dict[str, Any]:
    seeds = [args.seed + index for index in range(args.games)]
    candidate_theta = getattr(args, "candidate_theta", None)
    candidate = [_run_one(seed, "standard-v1", args.max_turns, args.min_food, candidate_theta) for seed in seeds]
    baseline = [_run_one(seed, "first-safe", args.max_turns, args.min_food, None) for seed in seeds]
    report = {
        "config": {
            "runner": "local",
            "games": args.games,
            "seed": args.seed,
            "max_turns": args.max_turns,
            "min_food": args.min_food,
            "latency_budget_ms": args.latency_budget_ms,
            "candidate": "standard-v1",
            "baseline": "first-safe",
            "opponents": ["first-safe", "first-safe", "first-safe"],
        },
        "candidate": _summarize_side(candidate, args.max_turns, args.latency_budget_ms),
        "baseline": _summarize_side(baseline, args.max_turns, args.latency_budget_ms),
        "paired": _paired_summary(candidate, baseline),
        "games": {
            "candidate": candidate,
            "baseline": baseline,
        },
    }
    report["candidate_beats_baseline"] = report["candidate"]["objective"] > report["baseline"]["objective"]
    return report


def _run_one(
    seed: int,
    controlled_strategy: str,
    max_turns: int,
    min_food: int,
    candidate_theta: dict[str, float] | None,
) -> dict[str, Any]:
    rng = Random(seed)
    board = _refill_food(
        Board(11, 11, _initial_snakes(), food=[], hazards=[], ruleset_name="standard", hazard_damage=15),
        rng,
        min_food,
    )
    strategies = {
        "standard-v1": StrategyStandard(theta=candidate_theta),
        "first-safe": StrategyFirstSafe(),
    }
    elimination_turns: dict[str, int] = {}
    death_causes: dict[str, str] = {}
    latencies: list[float] = []
    turn = 0

    while turn < max_turns and len(board.snakes) > 1:
        turn += 1
        moves: dict[str, str] = {}
        for snake_id in sorted(board.snakes):
            strategy_name = controlled_strategy if snake_id == "snake-0" else "first-safe"
            start = time.perf_counter()
            move = strategies[strategy_name].decide(board, snake_id)
            elapsed_ms = (time.perf_counter() - start) * 1000.0
            if snake_id == "snake-0":
                latencies.append(elapsed_ms)
            moves[snake_id] = _move_value(move)

        before = set(board.snakes)
        board = board.clone_and_apply(moves)
        board = _refill_food(board, rng, min_food)
        for eliminated in sorted(before - set(board.snakes)):
            elimination_turns[eliminated] = turn
            death_causes[eliminated] = _classify_move_death(moves.get(eliminated, "up"))

    alive = set(board.snakes)
    placement = _placement(elimination_turns, "snake-0", alive, max_turns)
    death_cause = "won" if placement == 1 and "snake-0" in alive else "alive" if "snake-0" in alive else death_causes.get("snake-0", "unknown")
    return {
        "seed": seed,
        "strategy": controlled_strategy,
        "placement": placement,
        "placement_score": PLACEMENT_POINTS[placement],
        "turns": turn,
        "turn_score": turn / max_turns,
        "death_cause": death_cause,
        "latency_ms": latencies,
    }


def _summarize_side(games: list[dict[str, Any]], max_turns: int, latency_budget_ms: float) -> dict[str, Any]:
    placements = Counter(game["placement"] for game in games)
    death_causes = Counter(game["death_cause"] for game in games)
    latencies = [sample for game in games for sample in game["latency_ms"]]
    timeout_rate = sum(1 for sample in latencies if sample > latency_budget_ms) / len(latencies) if latencies else 0.0
    placement_score = mean(game["placement_score"] for game in games)
    turn_score = mean(game["turn_score"] for game in games)
    death_penalty = mean(DEATH_PENALTY.get(game["death_cause"], DEATH_PENALTY["unknown"]) for game in games)
    objective = placement_score + 0.15 * turn_score - 0.25 * death_penalty - 0.50 * timeout_rate
    p95 = _percentile(latencies, 95)
    per_game_objectives = [
        game["placement_score"]
        + 0.15 * game["turn_score"]
        - 0.25 * DEATH_PENALTY.get(game["death_cause"], DEATH_PENALTY["unknown"])
        for game in games
    ]
    return {
        "objective": objective,
        "objective_ci95": _ci95(per_game_objectives),
        "placement_score": placement_score,
        "turn_score": turn_score,
        "death_penalty": death_penalty,
        "timeout_rate": timeout_rate,
        "latency_gate_passed": p95 <= latency_budget_ms,
        "placements": {str(key): placements.get(key, 0) for key in range(1, 5)},
        "death_causes": dict(sorted(death_causes.items())),
        "latency_ms": {
            "p50": _percentile(latencies, 50),
            "p95": p95,
            "p99": _percentile(latencies, 99),
            "count": len(latencies),
        },
        "max_turns": max_turns,
    }


def _paired_summary(candidate: list[dict[str, Any]], baseline: list[dict[str, Any]]) -> dict[str, Any]:
    placement_deltas = [base["placement"] - cand["placement"] for cand, base in zip(candidate, baseline)]
    score_deltas = [cand["placement_score"] - base["placement_score"] for cand, base in zip(candidate, baseline)]
    return {
        "mean_placement_delta": mean(placement_deltas),
        "mean_placement_score_delta": mean(score_deltas),
        "placement_delta_ci95": _ci95(placement_deltas),
        "score_delta_ci95": _ci95(score_deltas),
    }


def format_summary(report: dict[str, Any]) -> str:
    candidate = report["candidate"]
    baseline = report["baseline"]
    paired = report["paired"]
    return "\n".join(
        [
            "Standard FFA paired arena",
            f"games={report['config']['games']} seed={report['config']['seed']} runner={report['config']['runner']}",
            f"candidate objective={candidate['objective']:.4f} placement_score={candidate['placement_score']:.4f} latency_p95={candidate['latency_ms']['p95']:.3f} gate={candidate['latency_gate_passed']}",
            f"baseline  objective={baseline['objective']:.4f} placement_score={baseline['placement_score']:.4f} latency_p95={baseline['latency_ms']['p95']:.3f} gate={baseline['latency_gate_passed']}",
            f"paired mean_placement_delta={paired['mean_placement_delta']:.3f} score_delta={paired['mean_placement_score_delta']:.4f}",
            f"candidate placements={candidate['placements']} deaths={candidate['death_causes']}",
            f"baseline placements={baseline['placements']} deaths={baseline['death_causes']}",
        ]
    )


def _initial_snakes() -> dict[str, Snake]:
    width = 11
    height = 11
    starts = [
        [Coord(1, 1), Coord(1, 0), Coord(0, 0)],
        [Coord(width - 2, height - 2), Coord(width - 2, height - 1), Coord(width - 1, height - 1)],
        [Coord(width - 2, 1), Coord(width - 1, 1), Coord(width - 1, 0)],
        [Coord(1, height - 2), Coord(0, height - 2), Coord(0, height - 1)],
    ]
    return {
        f"snake-{index}": Snake(id=f"snake-{index}", name=f"snake-{index}", health=100, body=body, length=len(body))
        for index, body in enumerate(starts)
    }


def _refill_food(board: Board, rng: Random, min_food: int) -> Board:
    if len(board.food) >= min_food:
        return board
    occupied = board.occupied(include_tails=True)
    food = set(board.food)
    available = [
        Coord(x, y)
        for x in range(board.width)
        for y in range(board.height)
        if Coord(x, y) not in occupied and Coord(x, y) not in food and Coord(x, y) not in board.hazards
    ]
    rng.shuffle(available)
    while len(food) < min_food and available:
        food.add(available.pop())
    return Board(board.width, board.height, board.snakes, food=food, hazards=board.hazards, ruleset_name=board.ruleset_name, hazard_damage=board.hazard_damage)


def _placement(elimination_turns: dict[str, int], snake_id: str, alive: set[str], max_turns: int) -> int:
    if snake_id in alive:
        return 1 if len(alive) == 1 else len(alive)
    eliminated_at = elimination_turns.get(snake_id, max_turns + 1)
    later = len(alive) + sum(1 for turn in elimination_turns.values() if turn > eliminated_at)
    return min(4, later + 1)


def _classify_move_death(move: str) -> str:
    # The local runner only sees post-resolution state. Keep v1 death labels
    # conservative; #20 live telemetry provides richer ladder death causes.
    return "unknown" if move else "unknown"


def _move_value(move: object) -> str:
    return move.value if hasattr(move, "value") else str(move)


def _percentile(values: list[float], percentile: int) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    if percentile == 50:
        return float(median(ordered))
    index = max(0, min(len(ordered) - 1, int((percentile / 100.0) * len(ordered)) - 1))
    return ordered[index]


def _ci95(values: list[float]) -> dict[str, float]:
    if not values:
        return {"mean": 0.0, "half_width": 0.0, "low": 0.0, "high": 0.0}
    avg = mean(values)
    if len(values) == 1:
        return {"mean": avg, "half_width": 0.0, "low": avg, "high": avg}
    variance = sum((value - avg) ** 2 for value in values) / (len(values) - 1)
    half_width = 1.96 * math.sqrt(variance / len(values))
    return {"mean": avg, "half_width": half_width, "low": avg - half_width, "high": avg + half_width}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--games", type=int, default=20)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--max-turns", type=int, default=120)
    parser.add_argument("--min-food", type=int, default=3)
    parser.add_argument("--latency-budget-ms", type=float, default=80.0)
    parser.add_argument("--output", type=Path, default=Path("benchmarks/results/standard-ffa-arena.json"))
    parser.add_argument("--summary-output", type=Path)
    parser.add_argument("--candidate-theta", type=Path, help="JSON theta override for standard-v1")
    parser.add_argument("--no-fail-on-latency", action="store_true")
    args = parser.parse_args()
    if args.candidate_theta:
        args.candidate_theta = json.loads(args.candidate_theta.read_text(encoding="utf-8"))

    report = run_paired_arena(args)
    summary = format_summary(report)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    if args.summary_output:
        args.summary_output.parent.mkdir(parents=True, exist_ok=True)
        args.summary_output.write_text(summary + "\n", encoding="utf-8")
    print(summary)

    if not args.no_fail_on_latency and not report["candidate"]["latency_gate_passed"]:
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
