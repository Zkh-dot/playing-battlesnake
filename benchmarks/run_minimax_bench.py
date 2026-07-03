from __future__ import annotations

import argparse
import json
import statistics
import time
from pathlib import Path
from typing import Any

from battlesnake.battlesnake_native import minimax_diagnostics
from benchmarks.scenarios import SCENARIOS, build_board


def percentile(values: list[float], pct: float) -> float:
    if not values:
        return 0.0

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
        "move": statistics.mode([str(row["move"]) for row in rows]) if rows else "",
        "completed_depth": percentile(_numeric(rows, "completed_depth"), 0.50),
        "max_depth_started": percentile(_numeric(rows, "max_depth_started"), 0.50),
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
        "tt_hit_rate_p50": percentile(tt_hit_rates, 0.50),
        "tt_cutoffs_p50": percentile(_numeric(rows, "tt_cutoffs"), 0.50),
        "tt_stores_p50": percentile(_numeric(rows, "tt_stores"), 0.50),
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
    return [int(part.strip()) for part in value.split(",") if part.strip()]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--runs", type=int, default=20)
    parser.add_argument("--warmup", type=int, default=3)
    parser.add_argument("--budgets", default="180,320,450")
    parser.add_argument("--fixed-depths", default="0,4,6")
    parser.add_argument("--out", type=Path, default=Path("benchmarks/results/minimax-bench.jsonl"))
    args = parser.parse_args()

    budgets = sorted(_parse_ints(args.budgets))
    fixed_depths = sorted(_parse_ints(args.fixed_depths))

    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("w", encoding="utf-8") as out:
        for scenario in sorted(SCENARIOS, key=lambda item: item.name):
            for budget_ms in budgets:
                for fixed_depth in fixed_depths:
                    row = run_case(
                        scenario.name,
                        budget_ms,
                        fixed_depth,
                        args.runs,
                        args.warmup,
                    )
                    row["started_at"] = time.time()
                    line = json.dumps(row, sort_keys=True)
                    out.write(line + "\n")
                    out.flush()
                    print(line, flush=True)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
