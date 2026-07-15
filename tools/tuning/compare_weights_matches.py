#!/usr/bin/env python3
"""Run a seeded paired Standard-duel comparison of two weight profiles."""

from __future__ import annotations

import argparse
import copy
import json
import math
import os
import platform
import random
import resource
import socket
import subprocess
import sys
import time
from dataclasses import dataclass
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
    first_profile: Agent


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
    first_profile: Agent = "before"


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
        first_profile: Agent = "after" if rng.randrange(2) else "before"
        for index, after_side in enumerate((first_after_side, 1 - first_after_side)):
            search_first: Agent = first_profile if index == 0 else ("before" if first_profile == "after" else "after")
            scheduled.append(ScheduledMatch(len(scheduled), pair, scenario, after_side, search_first))
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
               first_profile: Agent,
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
        arguments = {
            "before": (before_weights, 1 - after_side),
            "after": (after_weights, after_side),
        }
        search_order = (first_profile, "after" if first_profile == "before" else "before")
        selected: dict[str, MoveResult] = {}
        for profile in search_order:
            weights, physical_side = arguments[profile]
            selected[profile] = _choose_move(
                board, profile, weights, turn=turn, physical_side=physical_side,
                fixed_depth=fixed_depth, time_budget_ms=time_budget_ms,
            )
        before = selected["before"]
        after = selected["after"]
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
                       before_health, after_health, tuple(moves), first_profile)


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


MATCH_FIELDS = (
    "match", "pair", "scenario", "after_side", "first_profile", "winner", "turns",
    "before_alive", "after_alive", "before_length", "after_length",
    "before_health", "after_health",
)
EVENT_FIELDS = ("match", "turn", "physical_side", "move")


def _event(row: MatchResult, move: MoveResult, detail: str | None = None) -> list[object]:
    event: list[object] = [row.match, move.turn, move.physical_side, move.move]
    if detail is not None:
        event.append(detail)
    return event


def compact_evidence(results: list[MatchResult]) -> dict[str, object]:
    measurements: dict[str, dict[str, object]] = {}
    for profile in ("before", "after"):
        moves = [move for row in results for move in row.moves if move.profile == profile]
        measurements[profile] = {
            "latency_ms": sorted(float(move.elapsed_ms) for move in moves if move.elapsed_ms is not None),
            "timeouts": [_event(row, move) for row in results for move in row.moves if move.profile == profile and move.timed_out],
            "search_errors": [_event(row, move, move.error) for row in results for move in row.moves if move.profile == profile and move.error is not None],
            "audit_errors": [_event(row, move, move.audit_error) for row in results for move in row.moves if move.profile == profile and move.audit_error is not None],
            "structural_risks": [_event(row, move) for row in results for move in row.moves if move.profile == profile and move.structural_risk],
            "policy_violations": [_event(row, move) for row in results for move in row.moves if move.profile == profile and move.policy_violation],
        }
    return {
        "match_fields": list(MATCH_FIELDS),
        "event_fields": list(EVENT_FIELDS),
        "error_event_fields": [*EVENT_FIELDS, "error"],
        "matches": [[getattr(row, field) for field in MATCH_FIELDS] for row in results],
        "measurements": measurements,
    }


def _decoded_matches(raw: dict[str, object]) -> list[dict[str, object]]:
    fields = raw["match_fields"]
    if fields != list(MATCH_FIELDS):
        raise ValueError("unexpected compact match fields")
    return [dict(zip(fields, values, strict=True)) for values in raw["matches"]]


def summarize_compact(raw: dict[str, object]) -> dict[str, object]:
    matches = _decoded_matches(raw)
    pairs: dict[int, list[dict[str, object]]] = {}
    for result in matches:
        pairs.setdefault(int(result["pair"]), []).append(result)
    paired = {"after_sweeps": 0, "before_sweeps": 0, "split_pairs": 0, "pairs_with_draw": 0}
    for pair_results in pairs.values():
        winners = [row["winner"] for row in pair_results]
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
        measured = raw["measurements"][profile]
        latencies = [float(value) for value in measured["latency_ms"]]
        alive_field = f"{profile}_alive"
        length_field = f"{profile}_length"
        profiles[profile] = {
            "games": len(matches),
            "wins": sum(row["winner"] == profile for row in matches),
            "losses": sum(row["winner"] not in {profile, "draw"} for row in matches),
            "draws": sum(row["winner"] == "draw" for row in matches),
            "terminal_survivals": sum(row["winner"] == profile and row[alive_field] for row in matches),
            "alive_at_cap": sum(row["winner"] == "draw" and row[alive_field] for row in matches),
            "alive_at_cap_rate": sum(row["winner"] == "draw" and row[alive_field] for row in matches) / len(matches) if matches else 0.0,
            "final_length_total": sum(int(row[length_field]) for row in matches),
            "move_count": len(latencies) + len(measured["search_errors"]),
            "search_errors": len(measured["search_errors"]),
            "audit_errors": len(measured["audit_errors"]),
            "search_timeouts": len(measured["timeouts"]),
            "structural_risk_selections": len(measured["structural_risks"]),
            "policy_violations": len(measured["policy_violations"]),
            "physical_side_games": {str(side): sum((row["after_side"] if profile == "after" else 1 - int(row["after_side"])) == side for row in matches) for side in (0, 1)},
            "latency_ms": {"source": "native minimax_diagnostics.elapsed_ms", "semantics": "nearest-rank",
                           "p50": percentile(latencies, .50), "p95": percentile(latencies, .95),
                           "p99": percentile(latencies, .99), "max": max(latencies, default=0.0)},
        }
    decisive = paired["after_sweeps"] + paired["before_sweeps"]
    return {
        "matches": len(matches), "pairs": len(pairs),
        "shared_match_duration": {
            "turns_total": sum(int(row["turns"]) for row in matches),
            "turns_mean": sum(int(row["turns"]) for row in matches) / len(matches) if matches else 0.0,
        },
        "paired_outcomes": paired,
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


def summarize(results: list[MatchResult]) -> dict[str, object]:
    return summarize_compact(compact_evidence(results))


def _format_number_array(values: list[float], *, per_line: int = 12) -> str:
    if not values:
        return "[]"
    lines = []
    for offset in range(0, len(values), per_line):
        chunk = ", ".join(json.dumps(value, allow_nan=False) for value in values[offset:offset + per_line])
        lines.append("        " + chunk)
    return "[\n" + ",\n".join(lines) + "\n      ]"


def write_compact_json(path: Path, payload: dict[str, object]) -> None:
    shadow = copy.deepcopy(payload)
    markers = {}
    for profile in ("before", "after"):
        marker = f"__{profile}_latency_samples__"
        values = shadow["raw"]["measurements"][profile]["latency_ms"]
        markers[marker] = _format_number_array(values)
        shadow["raw"]["measurements"][profile]["latency_ms"] = marker
    serialized = json.dumps(shadow, indent=2, sort_keys=True, allow_nan=False)
    for marker, replacement in markers.items():
        serialized = serialized.replace(json.dumps(marker), replacement)
    path.write_text(serialized + "\n")


def write_markdown(path: Path, summary: dict[str, object]) -> None:
    path.write_text("# Generated duel weight comparison\n\n```json\n" + json.dumps(summary, indent=2, sort_keys=True) + "\n```\n")


def _experiment_environment() -> dict[str, object]:
    status = subprocess.run(
        ["git", "status", "--porcelain"], cwd=REPO_ROOT, text=True,
        capture_output=True, check=True,
    ).stdout
    if status:
        raise RuntimeError("experiment must start from a clean git worktree")
    commit = subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=REPO_ROOT, text=True,
    ).strip()
    cpu_model = "unknown"
    try:
        for line in Path("/proc/cpuinfo").read_text().splitlines():
            if line.startswith("model name"):
                cpu_model = line.split(":", 1)[1].strip()
                break
    except OSError:
        pass
    try:
        compiler = subprocess.check_output(
            ["x86_64-linux-gnu-gcc", "--version"], text=True,
        ).splitlines()[0]
    except (OSError, subprocess.SubprocessError):
        compiler = "unknown"
    return {
        "experiment_input_commit": commit,
        "host": socket.gethostname(),
        "kernel": platform.release(),
        "architecture": platform.machine(),
        "cpu": cpu_model,
        "logical_cpus": os.cpu_count(),
        "python": platform.python_version(),
        "compiler": compiler,
    }


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
    environment = _experiment_environment()
    started = time.perf_counter()
    before_profile = load_profile(args.before_weights)
    after_profile = load_profile(args.after_weights)
    schedule = experiment_schedule(seed=args.seed, scenario_count=args.scenario_count)
    results = [play_match(match_index=row.match, pair=row.pair, scenario=row.scenario,
                          after_side=row.after_side, first_profile=row.first_profile,
                          before_weights=dict(before_profile.weights),
                          after_weights=dict(after_profile.weights), fixed_depth=args.fixed_depth,
                          time_budget_ms=args.time_budget_ms, max_turns=args.max_turns) for row in schedule]
    summary = summarize(results)
    environment["wall_seconds"] = time.perf_counter() - started
    environment["max_rss_kb"] = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    payload = {
        "schema_version": 3,
        "settings": {"seed": args.seed, "scenario_count": args.scenario_count,
                     "fixed_depth": args.fixed_depth, "time_budget_ms": args.time_budget_ms,
                     "max_turns": args.max_turns, "paired_games_per_scenario": 2},
        "profiles": {"before": {"identifier": f"{before_profile.name}@{before_profile.version}", "sha256": before_profile.sha256},
                     "after": {"identifier": f"{after_profile.name}@{after_profile.version}", "sha256": after_profile.sha256}},
        "environment": environment,
        "summary": summary,
        "raw": compact_evidence(results),
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    write_compact_json(args.output, payload)
    write_markdown(args.markdown_output, summary)
    print(json.dumps(summary, sort_keys=True))
    return 1 if any(
        profile["search_errors"] or profile["audit_errors"]
        for profile in summary["profiles"].values()
    ) else 0


if __name__ == "__main__":
    raise SystemExit(main())
