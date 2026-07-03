from __future__ import annotations

import argparse
import json
import os
import statistics
import time
from pathlib import Path
from typing import Any

from battlesnake.battlesnake_native import minimax_diagnostics
from benchmarks.scenarios import SCENARIOS, build_board

MAX_FIXED_DEPTH = 32


def percentile(values: list[float], pct: float) -> float:
    if not values:
        return 0.0
    if pct == 0.50:
        return float(statistics.median(values))

    sorted_values = sorted(values)
    index = int(round((len(sorted_values) - 1) * pct))
    return float(sorted_values[index])


def _numeric(rows: list[dict[str, Any]], key: str) -> list[float]:
    return [float(row[key]) for row in rows]


def summarize(
    rows: list[dict[str, Any]],
    scenario: str,
    budget_ms: int,
    fixed_depth: int,
    runs: int,
    warmup: int,
) -> dict[str, Any]:
    elapsed_ms = _numeric(rows, "elapsed_ms")
    nodes = _numeric(rows, "nodes")
    tt_probes = _numeric(rows, "tt_probes")
    move_counts: dict[str, int] = {}
    for row in rows:
        move = str(row["move"])
        move_counts[move] = move_counts.get(move, 0) + 1
    tt_hit_rates = [
        float(row["tt_hits"]) / float(row["tt_probes"]) if float(row["tt_probes"]) else 0.0
        for row in rows
    ]
    nodes_per_second = [
        float(row["nodes"]) / (float(row["elapsed_ms"]) / 1000.0)
        if float(row["elapsed_ms"]) > 0.0
        else 0.0
        for row in rows
    ]

    return {
        "scenario": scenario,
        "budget_ms": budget_ms,
        "fixed_depth": fixed_depth,
        "runs": runs,
        "warmup": warmup,
        "move": rows[-1]["move"] if rows else "",
        "move_counts": move_counts,
        "completed_depth": float(statistics.median(int(row["completed_depth"]) for row in rows)) if rows else 0.0,
        "max_depth_started": max(int(row["max_depth_started"]) for row in rows) if rows else 0,
        "timed_out_count": sum(1 for row in rows if row["timed_out"]),
        "elapsed_ms_min": min(elapsed_ms) if elapsed_ms else 0.0,
        "elapsed_ms_p50": percentile(elapsed_ms, 0.50),
        "elapsed_ms_p95": percentile(elapsed_ms, 0.95),
        "elapsed_ms_max": max(elapsed_ms) if elapsed_ms else 0.0,
        "nodes_min": min(nodes) if nodes else 0.0,
        "nodes_p50": percentile(nodes, 0.50),
        "nodes_max": max(nodes) if nodes else 0.0,
        "nodes_per_second_p50": percentile(nodes_per_second, 0.50),
        "leaf_evals_p50": percentile(_numeric(rows, "leaf_evals"), 0.50),
        "clone_calls_p50": percentile(_numeric(rows, "clone_calls"), 0.50),
        "board_allocations_p50": percentile(_numeric(rows, "board_allocations"), 0.50),
        "beta_cutoffs_p50": percentile(_numeric(rows, "beta_cutoffs"), 0.50),
        "move_order_first_choice_cutoffs_p50": percentile(
            _numeric(rows, "move_order_first_choice_cutoffs"), 0.50
        ),
        "tt_probes_p50": percentile(tt_probes, 0.50),
        "tt_hits_p50": percentile(_numeric(rows, "tt_hits"), 0.50),
        "tt_exact_hits_p50": percentile(_numeric(rows, "tt_exact_hits"), 0.50),
        "tt_lower_hits_p50": percentile(_numeric(rows, "tt_lower_hits"), 0.50),
        "tt_upper_hits_p50": percentile(_numeric(rows, "tt_upper_hits"), 0.50),
        "tt_hit_rate_p50": percentile(tt_hit_rates, 0.50),
        "tt_cutoffs_p50": percentile(_numeric(rows, "tt_cutoffs"), 0.50),
        "tt_stores_p50": percentile(_numeric(rows, "tt_stores"), 0.50),
        "tt_collisions_p50": percentile(_numeric(rows, "tt_collisions"), 0.50),
    }


def run_case(
    scenario_name: str,
    budget_ms: int,
    fixed_depth: int,
    runs: int,
    warmup: int,
) -> dict[str, Any]:
    scenario = next(scenario for scenario in SCENARIOS if scenario.name == scenario_name)
    rows: list[dict[str, Any]] = []

    for index in range(warmup + runs):
        board = build_board(scenario)
        diagnostics = minimax_diagnostics(
            board,
            scenario.snake_id,
            time_budget_ms=budget_ms,
            fixed_depth=fixed_depth,
            enable_tt=True,
            enable_move_ordering=True,
            enable_make_unmake=True,
        )
        if index >= warmup:
            rows.append(diagnostics)

    return summarize(rows, scenario_name, budget_ms, fixed_depth, runs, warmup)


def _parse_ints(value: str) -> list[int]:
    values = [int(part.strip()) for part in value.split(",") if part.strip()]
    if not values:
        raise argparse.ArgumentTypeError("must contain at least one integer")
    if len(values) != len(set(values)):
        raise argparse.ArgumentTypeError("must not contain duplicate integers")
    return values


def _validate_args(parser: argparse.ArgumentParser, args: argparse.Namespace) -> tuple[list[int], list[int]]:
    if args.runs < 1:
        parser.error("--runs must be at least 1")
    if args.warmup < 0:
        parser.error("--warmup must be non-negative")

    try:
        budgets = _parse_ints(args.budgets)
        fixed_depths = _parse_ints(args.fixed_depths)
    except ValueError as error:
        parser.error(str(error))
    except argparse.ArgumentTypeError as error:
        parser.error(str(error))

    if any(value < 1 for value in budgets):
        parser.error("--budgets values must be positive")
    if any(value < 0 or value > MAX_FIXED_DEPTH for value in fixed_depths):
        parser.error(f"--fixed-depths values must be between 0 and {MAX_FIXED_DEPTH}")

    return budgets, fixed_depths


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--runs", type=int, default=20)
    parser.add_argument("--warmup", type=int, default=3)
    parser.add_argument("--budgets", default="180,320,450")
    parser.add_argument("--fixed-depths", default="0,4,6")
    parser.add_argument("--out", type=Path, default=Path("benchmarks/results/minimax-bench.jsonl"))
    args = parser.parse_args()

    budgets, fixed_depths = _validate_args(parser, args)

    args.out.parent.mkdir(parents=True, exist_ok=True)
    started = time.strftime("%Y-%m-%dT%H:%M:%S%z")
    tmp_path = args.out.with_name(f".{args.out.name}.tmp.{os.getpid()}")
    with tmp_path.open("w", encoding="utf-8") as out:
        for scenario in SCENARIOS:
            for budget_ms in budgets:
                for fixed_depth in fixed_depths:
                    row = run_case(
                        scenario.name,
                        budget_ms,
                        fixed_depth,
                        args.runs,
                        args.warmup,
                    )
                    row["started_at"] = started
                    line = json.dumps(row, sort_keys=True)
                    out.write(line + "\n")
                    out.flush()
                    print(line, flush=True)

    tmp_path.replace(args.out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
