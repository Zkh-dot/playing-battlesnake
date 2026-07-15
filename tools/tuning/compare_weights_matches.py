#!/usr/bin/env python3
"""Run a seeded paired Standard-duel comparison of two weight profiles."""

from __future__ import annotations

import argparse
import json
import math
import random
import sys
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Literal

REPO_ROOT = Path(__file__).resolve().parents[2]
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from battlesnake.battlesnake_native import Board, Coord, Snake, minimax_diagnostics
from benchmarks.scenarios import Scenario
from tools.check_duel_structural_policy import _complete_opponent_replies, audit_diagnostics
from tools.tuning.duel_weight_profiles import load_profile


Agent = Literal["before", "after"]
Winner = Literal["before", "after", "draw"]


@dataclass(frozen=True)
class ScheduledMatch:
    match: int
    pair: int
    scenario: Scenario
    after_side: int


@dataclass(frozen=True)
class MoveResult:
    profile: Agent
    turn: int
    physical_side: int
    move: str
    elapsed_ms: float | None
    timed_out: bool
    structural_risk: bool
    policy_violation: bool
    error: str | None
    audit_error: str | None = None


@dataclass(frozen=True)
class MatchResult:
    match: int
    pair: int
    scenario: str
    after_side: int
    winner: Winner
    turns: int
    before_alive: bool
    after_alive: bool
    before_length: int
    after_length: int
    before_health: int
    after_health: int
    moves: tuple[MoveResult, ...]


def _clone_snake(source: Snake, snake_id: Agent) -> Snake:
    return Snake(snake_id, snake_id, source.health, tuple(source.body), source.head, source.length)


def _generated_standard_duel_scenario(seed: int, pair: int) -> Scenario:
    rng = random.Random(seed)
    width = height = 11
    left_length = rng.randint(3, 5)
    right_length = rng.randint(3, 5)
    left_y = rng.randint(2, height - 3)
    right_y = rng.randint(2, height - 3)
    left_head_x = rng.randint(left_length - 1, 4)
    right_head_x = width - 1 - rng.randint(right_length - 1, 4)
    left_body = tuple(Coord(left_head_x - offset, left_y) for offset in range(left_length))
    right_body = tuple(Coord(right_head_x + offset, right_y) for offset in range(right_length))
    occupied = {(coord.x, coord.y) for coord in left_body + right_body}
    food: list[Coord] = []
    while len(food) < 5:
        coord = (rng.randint(1, width - 2), rng.randint(1, height - 2))
        if coord in occupied or coord in {(item.x, item.y) for item in food}:
            continue
        food.append(Coord(*coord))
    return Scenario(
        name=f"generated_standard_duel_{pair}_{seed}", width=width, height=height,
        ruleset_name="standard", hazard_damage=0,
        snakes=(
            Snake("left", "left", 90, left_body, left_body[0], left_length),
            Snake("right", "right", 90, right_body, right_body[0], right_length),
        ),
        food=tuple(food), hazards=(),
    )


def experiment_schedule(*, seed: int, scenario_count: int) -> list[ScheduledMatch]:
    if scenario_count <= 0:
        raise ValueError("scenario_count must be positive")
    rng = random.Random(seed)
    scheduled: list[ScheduledMatch] = []
    for pair in range(scenario_count):
        scenario_seed = rng.getrandbits(64)
        scenario = _generated_standard_duel_scenario(scenario_seed, pair)
        first_after_side = rng.randrange(2)
        for after_side in (first_after_side, 1 - first_after_side):
            scheduled.append(ScheduledMatch(len(scheduled), pair, scenario, after_side))
    return scheduled


def _build_match_board(scenario: Scenario, after_side: int) -> Board:
    before_side = 1 - after_side
    snakes = (_clone_snake(scenario.snakes[before_side], "before"), _clone_snake(scenario.snakes[after_side], "after"))
    return Board(scenario.width, scenario.height, snakes, scenario.food, scenario.hazards,
                 scenario.ruleset_name, scenario.hazard_damage)


def _snake_status(board: Board, snake_id: Agent) -> tuple[bool, int, int]:
    snake = board.snakes.get(snake_id)
    return (False, 0, 0) if snake is None else (True, snake.length, snake.health)


def _is_structural_risk(diagnostics: dict[str, object]) -> bool:
    candidates = diagnostics.get("root_candidates")
    move = diagnostics.get("move")
    if not isinstance(candidates, dict) or move not in candidates:
        return False
    selected = candidates[move]
    if not isinstance(selected, dict):
        return False
    try:
        deficient = int(selected["relaxed_static_capacity"]) < int(selected["post_move_length"])
    except (KeyError, TypeError, ValueError):
        return False
    safe_alternative = any(
        candidate_move != move
        and isinstance(candidate, dict)
        and candidate.get("structural_proof") == "safe"
        and int(candidate.get("alive_reply_count", 0)) > 0
        for candidate_move, candidate in candidates.items()
    )
    return deficient and selected.get("structural_proof") != "safe" and safe_alternative


def _choose_move(board: Board, snake_id: Agent, weights: dict[str, float], *, turn: int,
                 physical_side: int, fixed_depth: int, time_budget_ms: int) -> MoveResult:
    try:
        diagnostics = minimax_diagnostics(
            board, snake_id, time_budget_ms=time_budget_ms, fixed_depth=fixed_depth,
            enable_tt=True, enable_move_ordering=True, enable_make_unmake=True, weights=weights,
        )
    except Exception as exc:
        error = f"{type(exc).__name__}: {exc}"
        print(f"move selection failed for {snake_id}: {error}", file=sys.stderr)
        safe_moves = board.safe_moves(snake_id)
        return MoveResult(snake_id, turn, physical_side, safe_moves[0] if safe_moves else "up",
                          None, False, False, False, error)
    audit_error = None
    violation = False
    try:
        audit_input = dict(diagnostics)
        audit_input["complete_opponent_replies"] = _complete_opponent_replies(board, snake_id)
        violation = audit_diagnostics(audit_input).violation
    except Exception as exc:
        audit_error = f"{type(exc).__name__}: {exc}"
    return MoveResult(
        snake_id, turn, physical_side, str(diagnostics["move"]),
        float(diagnostics["elapsed_ms"]), bool(diagnostics["timed_out"]),
        _is_structural_risk(diagnostics), violation, None, audit_error,
    )


def _winner(board: Board) -> Winner | None:
    before_alive = "before" in board.snakes
    after_alive = "after" in board.snakes
    if before_alive and after_alive:
        return None
    if not before_alive and not after_alive:
        return "draw"
    return "before" if before_alive else "after"


def play_match(*, match_index: int, pair: int, scenario: Scenario, after_side: int,
               before_weights: dict[str, float], after_weights: dict[str, float],
               fixed_depth: int, time_budget_ms: int, max_turns: int) -> MatchResult:
    board = _build_match_board(scenario, after_side)
    moves: list[MoveResult] = []
    turns = 0
    result: Winner | None = None
    for turn in range(max_turns):
        result = _winner(board)
        if result is not None:
            turns = turn
            break
        before = _choose_move(board, "before", before_weights, turn=turn,
                              physical_side=1 - after_side, fixed_depth=fixed_depth, time_budget_ms=time_budget_ms)
        after = _choose_move(board, "after", after_weights, turn=turn,
                             physical_side=after_side, fixed_depth=fixed_depth, time_budget_ms=time_budget_ms)
        moves.extend((before, after))
        board = board.clone_and_apply({"before": before.move, "after": after.move})
        turns = turn + 1
        result = _winner(board)
        if result is not None:
            break
    else:
        result = "draw"
    before_alive, before_length, before_health = _snake_status(board, "before")
    after_alive, after_length, after_health = _snake_status(board, "after")
    return MatchResult(match_index, pair, scenario.name, after_side, result, turns,
                       before_alive, after_alive, before_length, after_length,
                       before_health, after_health, tuple(moves))


def percentile(values: list[float], quantile: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    return float(ordered[max(0, math.ceil(quantile * len(ordered)) - 1)])


def _exact_sign_pvalue(successes: int, failures: int) -> float:
    n = successes + failures
    if n == 0:
        return 1.0
    tail = sum(math.comb(n, k) for k in range(0, min(successes, failures) + 1)) / (2**n)
    return min(1.0, 2.0 * tail)


def _wilson_interval(successes: int, total: int) -> list[float]:
    if total == 0:
        return [0.0, 1.0]
    z = 1.959963984540054
    p = successes / total
    denominator = 1 + z * z / total
    center = (p + z * z / (2 * total)) / denominator
    margin = z * math.sqrt(p * (1 - p) / total + z * z / (4 * total * total)) / denominator
    return [max(0.0, center - margin), min(1.0, center + margin)]


def summarize(results: list[MatchResult]) -> dict[str, object]:
    pairs: dict[int, list[MatchResult]] = {}
    for result in results:
        pairs.setdefault(result.pair, []).append(result)
    paired = {"after_sweeps": 0, "before_sweeps": 0, "split_pairs": 0, "pairs_with_draw": 0}
    for pair_results in pairs.values():
        winners = [row.winner for row in pair_results]
        if "draw" in winners or len(winners) != 2:
            paired["pairs_with_draw"] += 1
        elif winners == ["after", "after"]:
            paired["after_sweeps"] += 1
        elif winners == ["before", "before"]:
            paired["before_sweeps"] += 1
        else:
            paired["split_pairs"] += 1

    profiles: dict[str, object] = {}
    for profile in ("before", "after"):
        latencies = [move.elapsed_ms for row in results for move in row.moves
                     if move.profile == profile and move.elapsed_ms is not None]
        alive = [row.before_alive if profile == "before" else row.after_alive for row in results]
        moves = [move for row in results for move in row.moves if move.profile == profile]
        lengths = [row.before_length if profile == "before" else row.after_length for row in results]
        profiles[profile] = {
            "games": len(results),
            "wins": sum(row.winner == profile for row in results),
            "losses": sum(row.winner not in {profile, "draw"} for row in results),
            "draws": sum(row.winner == "draw" for row in results),
            "turns_survived_total": sum(row.turns for row in results),
            "turns_survived_mean": sum(row.turns for row in results) / len(results) if results else 0.0,
            "terminal_survivals": sum(row.winner == profile and keep for row, keep in zip(results, alive)),
            "alive_at_cap": sum(row.winner == "draw" and keep for row, keep in zip(results, alive)),
            "alive_at_cap_rate": sum(row.winner == "draw" and keep for row, keep in zip(results, alive)) / len(results) if results else 0.0,
            "final_length_total": sum(lengths),
            "move_count": len(moves),
            "search_errors": sum(move.error is not None for move in moves),
            "audit_errors": sum(move.audit_error is not None for move in moves),
            "search_timeouts": sum(move.timed_out for move in moves),
            "structural_risk_selections": sum(move.structural_risk for move in moves),
            "policy_violations": sum(move.policy_violation for move in moves),
            "physical_side_games": {str(side): sum((row.after_side if profile == "after" else 1 - row.after_side) == side for row in results) for side in (0, 1)},
            "latency_ms": {"source": "native minimax_diagnostics.elapsed_ms", "semantics": "nearest-rank",
                           "p50": percentile(latencies, .50), "p95": percentile(latencies, .95),
                           "p99": percentile(latencies, .99), "max": max(latencies, default=0.0)},
        }
    decisive = paired["after_sweeps"] + paired["before_sweeps"]
    return {
        "matches": len(results), "pairs": len(pairs), "paired_outcomes": paired,
        "paired_uncertainty": {
            "method": "exact two-sided sign test on decisive non-split pairs",
            "unit": "paired scenario sweep; split and draw-containing pairs excluded",
            "decisive_pairs": decisive,
            "after_sweep_share": paired["after_sweeps"] / decisive if decisive else 0.0,
            "after_sweep_share_wilson_95": _wilson_interval(paired["after_sweeps"], decisive),
            "two_sided_p_value": _exact_sign_pvalue(paired["after_sweeps"], paired["before_sweeps"]),
        },
        "profiles": profiles,
    }


def write_markdown(path: Path, summary: dict[str, object]) -> None:
    path.write_text("# Generated duel weight comparison\n\n```json\n" + json.dumps(summary, indent=2, sort_keys=True) + "\n```\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before-weights", type=Path, default=Path("configs/evaluation_weights/default.json"))
    parser.add_argument("--after-weights", type=Path, required=True)
    parser.add_argument("--seed", type=int, required=True)
    parser.add_argument("--scenario-count", type=int, required=True)
    parser.add_argument("--fixed-depth", type=int, default=3)
    parser.add_argument("--time-budget-ms", type=int, default=300)
    parser.add_argument("--max-turns", type=int, default=200)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--markdown-output", type=Path, required=True)
    args = parser.parse_args()
    before_profile = load_profile(args.before_weights)
    after_profile = load_profile(args.after_weights)
    schedule = experiment_schedule(seed=args.seed, scenario_count=args.scenario_count)
    results = [play_match(match_index=row.match, pair=row.pair, scenario=row.scenario,
                          after_side=row.after_side, before_weights=dict(before_profile.weights),
                          after_weights=dict(after_profile.weights), fixed_depth=args.fixed_depth,
                          time_budget_ms=args.time_budget_ms, max_turns=args.max_turns) for row in schedule]
    summary = summarize(results)
    payload = {
        "schema_version": 2,
        "settings": {"seed": args.seed, "scenario_count": args.scenario_count,
                     "fixed_depth": args.fixed_depth, "time_budget_ms": args.time_budget_ms,
                     "max_turns": args.max_turns, "paired_games_per_scenario": 2},
        "profiles": {"before": {"identifier": f"{before_profile.name}@{before_profile.version}", "sha256": before_profile.sha256},
                     "after": {"identifier": f"{after_profile.name}@{after_profile.version}", "sha256": after_profile.sha256}},
        "summary": summary,
        "results": [asdict(result) for result in results],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")
    write_markdown(args.markdown_output, summary)
    print(json.dumps(summary, sort_keys=True))
    return 1 if any(profile["search_errors"] for profile in summary["profiles"].values()) else 0


if __name__ == "__main__":
    raise SystemExit(main())
