from __future__ import annotations

import argparse
import math
import statistics
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from fastapi.testclient import TestClient

from battlesnake.core.minimax import minimax_move
from battlesnake.game import board_from_game_state
from battlesnake.main import app, select_strategy
import battlesnake.strategies.duel as duel_strategy
from battlesnake.strategies.duel import StrategyDuel
from battlesnake.types import GameState
from benchmarks.scenarios import get_scenario, scenario_names


def scenario_to_payload(name: str, *, force_duel_ruleset: bool = True) -> dict:
    scenario = get_scenario(name)
    snakes = []
    for snake in scenario.snakes:
        body = [{"x": coord.x, "y": coord.y} for coord in snake.body]
        snakes.append(
            {
                "id": snake.id,
                "name": snake.name,
                "health": snake.health,
                "body": body,
                "head": body[0],
                "length": snake.length,
            }
        )
    snake_by_id = {snake["id"]: snake for snake in snakes}
    ruleset_name = "solo" if force_duel_ruleset else scenario.ruleset_name
    payload = {
        "game": {
            "id": "latency-check",
            "ruleset": {
                "name": ruleset_name,
                "settings": {"hazardDamagePerTurn": scenario.hazard_damage},
            },
        },
        "turn": 0,
        "board": {
            "height": scenario.height,
            "width": scenario.width,
            "food": [{"x": coord.x, "y": coord.y} for coord in scenario.food],
            "hazards": [{"x": coord.x, "y": coord.y} for coord in scenario.hazards],
            "snakes": snakes,
        },
        "you": snake_by_id[scenario.snake_id],
    }
    payload["_scenario_ruleset"] = scenario.ruleset_name
    return payload


def validate_minimax_preflight(state: GameState) -> None:
    board = board_from_game_state(state)
    move = minimax_move(board, state.you.id, StrategyDuel.time_budget_ms)
    if move.value not in {"up", "down", "left", "right"}:
        raise SystemExit(f"invalid minimax preflight move: {move}")


def validate_move_response(result: object) -> None:
    if not isinstance(result, dict):
        raise SystemExit(f"invalid /move response shape: expected object, got {result!r}")
    move_value = result.get("move")
    if move_value is None:
        raise SystemExit(f"invalid /move response shape: missing move key in {result!r}")
    if move_value not in {"up", "down", "left", "right"}:
        raise SystemExit(f"invalid /move response move: {move_value!r} in {result!r}")


class MinimaxCallTracker:
    def __init__(self) -> None:
        self.calls = 0
        self.errors = 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--scenario", default="duel_open_7x7", choices=scenario_names())
    parser.add_argument("--runs", type=int, default=15)
    parser.add_argument("--p95-limit-ms", type=float, default=450.0)
    parser.add_argument("--force-duel-ruleset", action=argparse.BooleanOptionalAction, default=True)
    args = parser.parse_args()

    if args.runs < 1:
        parser.error("--runs must be at least 1")
    if not math.isfinite(args.p95_limit_ms) or args.p95_limit_ms <= 0:
        parser.error("--p95-limit-ms must be finite and greater than 0")

    payload = scenario_to_payload(args.scenario, force_duel_ruleset=args.force_duel_ruleset)
    scenario_ruleset = payload.pop("_scenario_ruleset")
    state = GameState(**payload)
    strategy = select_strategy(state)
    strategy_name = type(strategy).__name__
    effective_ruleset = state.game.ruleset.name
    snake_count = len(state.board.snakes)
    if not isinstance(strategy, StrategyDuel):
        raise SystemExit(
            "expected StrategyDuel routing, got "
            f"strategy={strategy_name} effective_ruleset={effective_ruleset} snake_count={snake_count}"
        )
    validate_minimax_preflight(state)

    client = TestClient(app)
    timings: list[float] = []
    tracker = MinimaxCallTracker()
    original_minimax_move = duel_strategy.minimax_move

    def tracked_minimax_move(*call_args: object, **call_kwargs: object) -> object:
        tracker.calls += 1
        try:
            return original_minimax_move(*call_args, **call_kwargs)
        except Exception:
            tracker.errors += 1
            raise

    duel_strategy.minimax_move = tracked_minimax_move
    try:
        for run_index in range(args.runs):
            calls_before = tracker.calls
            started = time.perf_counter()
            response = client.post("/move", json=payload)
            elapsed_ms = (time.perf_counter() - started) * 1000.0
            timings.append(elapsed_ms)
            response.raise_for_status()
            result = response.json()
            validate_move_response(result)
            calls_delta = tracker.calls - calls_before
            if calls_delta != 1:
                raise SystemExit(
                    "expected exactly one minimax call for /move request, got "
                    f"{calls_delta} on run {run_index + 1}"
                )
            if tracker.errors:
                raise SystemExit(f"minimax raised during /move timing: minimax_errors={tracker.errors}")
    finally:
        duel_strategy.minimax_move = original_minimax_move

    if tracker.calls != args.runs:
        raise SystemExit(f"expected minimax_calls={args.runs}, got {tracker.calls}")
    if tracker.errors:
        raise SystemExit(f"minimax raised during /move timing: minimax_errors={tracker.errors}")

    p50 = statistics.median(timings)
    p95 = sorted(timings)[math.ceil(0.95 * len(timings)) - 1]
    max_ms = max(timings)
    print(
        f"scenario={args.scenario} scenario_ruleset={scenario_ruleset} "
        f"effective_ruleset={effective_ruleset} snake_count={snake_count} "
        f"strategy={strategy_name} minimax_preflight=ok runs={args.runs} "
        f"minimax_calls={tracker.calls} minimax_errors={tracker.errors} "
        f"p50_ms={p50:.3f} p95_ms={p95:.3f} max_ms={max_ms:.3f} "
        f"limit_ms={args.p95_limit_ms:.3f}"
    )
    return 0 if p95 <= args.p95_limit_ms else 2


if __name__ == "__main__":
    raise SystemExit(main())
