from __future__ import annotations

import argparse
import json
import random
from pathlib import Path
from typing import Any

from battlesnake.battlesnake_native import Board, Coord, Snake
from battlesnake.strategies.standard import DEFAULT_STANDARD_THETA, StrategyStandard
from tools.standard_ffa_arena import run_paired_arena


THETA_SEARCH_SPACE: dict[str, tuple[float, float]] = {
    "w_expected": (0.6, 1.6),
    "w_worst": (0.15, 0.8),
    "w_space_log": (35.0, 140.0),
    "w_space_ratio": (5.0, 40.0),
    "w_escape": (20.0, 95.0),
    "w_zero_escape": (350.0, 1200.0),
    "w_losing_h2h": (500_000.0, 1_500_000.0),
    "w_winning_h2h": (20.0, 250.0),
    "w_food_on_cell": (40.0, 320.0),
    "w_food_route": (40.0, 280.0),
    "w_contested_food": (250.0, 900.0),
    "w_pocket": (120.0, 850.0),
    "food_urgency_health": (25.0, 45.0),
    "pocket_space_per_length": (1.4, 4.2),
    "nearby_opponent_distance": (2.0, 6.0),
    "opponent_reachable_space": (0.0, 2.5),
    "territory_delta": (0.0, 4.0),
    "opponent_safe_moves": (0.0, 45.0),
}


class ArenaArgs:
    def __init__(
        self,
        *,
        games: int,
        seed: int,
        max_turns: int,
        min_food: int,
        latency_budget_ms: float,
        candidate_theta: dict[str, float],
    ) -> None:
        self.games = games
        self.seed = seed
        self.max_turns = max_turns
        self.min_food = min_food
        self.latency_budget_ms = latency_budget_ms
        self.candidate_theta = candidate_theta


def suggest_theta(seed: int) -> dict[str, float]:
    rng = random.Random(seed)
    theta = dict(DEFAULT_STANDARD_THETA)
    for key, (lower, upper) in THETA_SEARCH_SPACE.items():
        theta[key] = rng.uniform(lower, upper)
    return theta


def scenario_suite_passes(theta: dict[str, float]) -> bool:
    strategy = StrategyStandard(theta=theta)
    return (
        strategy.decide(_hungry_food_board(), "me") == "up"
        and strategy.decide(_suicidal_h2h_board(), "me") != "right"
        and strategy.decide(_pocket_food_board(), "me") != "up"
    )


def run_trial(
    *,
    trial: int,
    theta: dict[str, float],
    games: int,
    seed: int,
    max_turns: int,
    min_food: int,
    latency_budget_ms: float,
) -> dict[str, Any]:
    if not scenario_suite_passes(theta):
        return {"trial": trial, "status": "pruned_scenario_suite", "score": None, "theta": theta}
    report = run_paired_arena(
        ArenaArgs(
            games=games,
            seed=seed,
            max_turns=max_turns,
            min_food=min_food,
            latency_budget_ms=latency_budget_ms,
            candidate_theta=theta,
        )
    )
    if not report["candidate"]["latency_gate_passed"]:
        return {"trial": trial, "status": "pruned_latency", "score": None, "theta": theta, "report": report}
    return {
        "trial": trial,
        "status": "ok",
        "score": report["candidate"]["objective"],
        "theta": theta,
        "report": report,
    }


def run_search(args: argparse.Namespace) -> dict[str, Any]:
    best_score = float("-inf")
    best_theta = dict(DEFAULT_STANDARD_THETA)
    trials: list[dict[str, Any]] = []
    for trial in range(args.trials):
        theta = dict(DEFAULT_STANDARD_THETA) if trial == 0 else suggest_theta(args.seed + trial)
        row = run_trial(
            trial=trial,
            theta=theta,
            games=args.games,
            seed=args.seed,
            max_turns=args.max_turns,
            min_food=args.min_food,
            latency_budget_ms=args.latency_budget_ms,
        )
        trials.append(row)
        if row["status"] == "ok" and row["score"] is not None and row["score"] > best_score:
            best_score = float(row["score"])
            best_theta = theta
    return {"best_score": best_score, "best_theta": best_theta, "trials": trials}


def _hungry_food_board() -> Board:
    return Board(7, 7, [snake("me", [(2, 2), (2, 1), (2, 0)], health=20), snake("a", [(6, 6)]), snake("b", [(6, 0)])], food=[Coord(2, 3)])


def _suicidal_h2h_board() -> Board:
    return Board(7, 7, [snake("me", [(2, 2), (2, 1), (2, 0)]), snake("equal", [(4, 2), (4, 1), (4, 0)]), snake("far", [(6, 6)])])


def _pocket_food_board() -> Board:
    return Board(
        5,
        5,
        [
            snake("me", [(2, 1), (0, 0), (0, 1)]),
            snake("left-blocker", [(1, 2), (1, 1)]),
            snake("right-blocker", [(3, 2), (3, 1)]),
            snake("top-blocker", [(2, 3), (1, 3)]),
        ],
        food=[Coord(2, 2)],
    )


def snake(snake_id: str, body: list[tuple[int, int]], *, health: int = 100) -> Snake:
    return Snake(id=snake_id, name=snake_id, health=health, body=[Coord(x, y) for x, y in body])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trials", type=int, default=20)
    parser.add_argument("--games", type=int, default=20)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--max-turns", type=int, default=120)
    parser.add_argument("--min-food", type=int, default=3)
    parser.add_argument("--latency-budget-ms", type=float, default=80.0)
    parser.add_argument("--output", type=Path, default=Path("configs/evaluation_weights/standard-ffa-v1-tuned.json"))
    parser.add_argument("--trials-output", type=Path, default=Path("artifacts/standard_ffa_weight_tuning/trials.jsonl"))
    args = parser.parse_args()

    result = run_search(args)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result["best_theta"], indent=2, sort_keys=True) + "\n", encoding="utf-8")
    args.trials_output.parent.mkdir(parents=True, exist_ok=True)
    with args.trials_output.open("w", encoding="utf-8") as handle:
        for row in result["trials"]:
            handle.write(json.dumps(row, sort_keys=True) + "\n")
    print(json.dumps({"best_score": result["best_score"], "output": str(args.output), "trials_output": str(args.trials_output)}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
