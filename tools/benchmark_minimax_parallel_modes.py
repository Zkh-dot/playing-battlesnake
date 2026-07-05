from __future__ import annotations

import argparse
import json
import os
import statistics
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from battlesnake.battlesnake_native import minimax_diagnostics
from benchmarks.scenarios import SCENARIOS, build_board

DUEL_SCENARIOS = [
    "duel_open_7x7",
    "duel_center_pressure_11x11",
    "duel_low_health_food_race",
    "duel_tail_chase_trap",
    "duel_corridor_choke",
    "duel_late_game_long_bodies",
    "royale_hazard_ring_duel",
]

SUPPORTED_MODES = [
    "serial",
    "root_moves",
    "pv_root_moves",
    "root_replies",
    "ply1_tasks",
    "leaf_eval",
]

MAX_FIXED_DEPTH = 32


def parse_csv_ints(value: str) -> list[int]:
    values = [int(part.strip()) for part in value.split(",") if part.strip()]
    if not values:
        raise argparse.ArgumentTypeError("must contain at least one integer")
    return values


def parse_csv_strings(value: str) -> list[str]:
    values = [part.strip() for part in value.split(",") if part.strip()]
    if not values:
        raise argparse.ArgumentTypeError("must contain at least one value")
    return values


def median(values: list[float]) -> float:
    if not values:
        return 0.0
    return float(statistics.median(values))


def percentile(values: list[float], pct: float) -> float:
    if not values:
        return 0.0
    if pct == 0.50:
        return median(values)

    sorted_values = sorted(values)
    index = int(round((len(sorted_values) - 1) * pct))
    return float(sorted_values[index])


def _scenario_by_name(scenario_name: str) -> Any:
    for scenario in SCENARIOS:
        if scenario.name == scenario_name:
            return scenario
    raise ValueError(f"unknown scenario: {scenario_name}")


def _numeric(rows: list[dict[str, Any]], key: str) -> list[float]:
    return [float(row[key]) for row in rows]


def run_case(
    scenario_name: str,
    mode: str,
    threads: int,
    budget_ms: int,
    fixed_depth: int,
    runs: int,
    warmup: int,
) -> dict[str, Any]:
    scenario = _scenario_by_name(scenario_name)
    rows: list[dict[str, Any]] = []
    previous_threads = os.environ.get("OMP_NUM_THREADS")
    os.environ["OMP_NUM_THREADS"] = str(threads)

    try:
        for index in range(warmup + runs):
            diagnostics = minimax_diagnostics(
                build_board(scenario),
                scenario.snake_id,
                time_budget_ms=budget_ms,
                fixed_depth=fixed_depth,
                enable_tt=True,
                enable_move_ordering=True,
                enable_make_unmake=True,
                parallel_mode=mode,
            )
            if index >= warmup:
                rows.append(diagnostics)
    finally:
        if previous_threads is None:
            os.environ.pop("OMP_NUM_THREADS", None)
        else:
            os.environ["OMP_NUM_THREADS"] = previous_threads

    move_counts: dict[str, int] = {}
    for row in rows:
        move = str(row["move"])
        move_counts[move] = move_counts.get(move, 0) + 1

    return {
        "scenario": scenario_name,
        "mode": mode,
        "threads": threads,
        "budget_ms": budget_ms,
        "fixed_depth": fixed_depth,
        "runs": runs,
        "warmup": warmup,
        "move_counts": move_counts,
        "last_move": rows[-1]["move"] if rows else "",
        "score_p50": median(_numeric(rows, "score")),
        "elapsed_ms_p50": percentile(_numeric(rows, "elapsed_ms"), 0.50),
        "elapsed_ms_p95": percentile(_numeric(rows, "elapsed_ms"), 0.95),
        "nodes_p50": median(_numeric(rows, "nodes")),
        "completed_depth_p50": median(_numeric(rows, "completed_depth")),
        "max_depth_started_max": max((int(row["max_depth_started"]) for row in rows), default=0),
        "timed_out_count": sum(1 for row in rows if row["timed_out"]),
        "parallel_workers_used_max": max(
            (int(row["parallel_workers_used"]) for row in rows),
            default=0,
        ),
    }


def _parse_args(parser: argparse.ArgumentParser, args: argparse.Namespace) -> None:
    if args.run_single_case_json:
        args.modes = [args.single_mode]
        args.scenarios = [args.single_scenario]
        args.threads = [args.single_threads]
        args.budgets = [args.single_budget_ms]
        args.fixed_depths = [args.single_fixed_depth]
    elif args.quick:
        args.modes = ["serial"]
        args.scenarios = ["duel_open_7x7"]
        args.threads = [1]
        args.budgets = [50]
        args.fixed_depths = [2]
        args.runs = 1
        args.warmup = 0
    else:
        try:
            args.modes = parse_csv_strings(args.modes)
            args.scenarios = parse_csv_strings(args.scenarios)
            args.threads = parse_csv_ints(args.threads)
            args.budgets = parse_csv_ints(args.budgets)
            args.fixed_depths = parse_csv_ints(args.fixed_depths)
        except ValueError as error:
            parser.error(str(error))
        except argparse.ArgumentTypeError as error:
            parser.error(str(error))

    if any(value is None for value in args.modes + args.scenarios):
        parser.error("internal single-case arguments are incomplete")
    if any(value is None for value in args.threads + args.budgets + args.fixed_depths):
        parser.error("internal single-case arguments are incomplete")

    unknown_modes = [mode for mode in args.modes if mode not in SUPPORTED_MODES]
    if unknown_modes:
        parser.error(f"unknown mode: {unknown_modes[0]}")

    if args.runs < 1:
        parser.error("--runs must be at least 1")
    if args.warmup < 0:
        parser.error("--warmup must be non-negative")
    if any(value < 1 for value in args.threads):
        parser.error("--threads values must be positive")
    if any(value < 1 for value in args.budgets):
        parser.error("--budgets values must be positive")
    if any(value < 0 or value > MAX_FIXED_DEPTH for value in args.fixed_depths):
        parser.error(f"--fixed-depths values must be between 0 and {MAX_FIXED_DEPTH}")

    scenario_names = {scenario.name for scenario in SCENARIOS}
    unknown_scenarios = [name for name in args.scenarios if name not in scenario_names]
    if unknown_scenarios:
        parser.error(f"unknown scenario: {unknown_scenarios[0]}")


def run_single_case_child(
    scenario_name: str,
    mode: str,
    threads: int,
    budget_ms: int,
    fixed_depth: int,
    runs: int,
    warmup: int,
    started: str,
) -> dict[str, Any]:
    env = os.environ.copy()
    env["OMP_NUM_THREADS"] = str(threads)
    command = [
        sys.executable,
        "-B",
        str(Path(__file__).resolve()),
        "--run-single-case-json",
        "--single-scenario",
        scenario_name,
        "--single-mode",
        mode,
        "--single-threads",
        str(threads),
        "--single-budget-ms",
        str(budget_ms),
        "--single-fixed-depth",
        str(fixed_depth),
        "--runs",
        str(runs),
        "--warmup",
        str(warmup),
    ]
    result = subprocess.run(
        command,
        check=False,
        env=env,
        text=True,
        capture_output=True,
    )
    if result.returncode != 0:
        if result.stdout:
            print(result.stdout, file=sys.stderr, end="")
        if result.stderr:
            print(result.stderr, file=sys.stderr, end="")
        raise RuntimeError(
            "single-case benchmark failed: "
            f"scenario={scenario_name} mode={mode} threads={threads} "
            f"budget_ms={budget_ms} fixed_depth={fixed_depth}"
        )

    line = result.stdout.strip()
    row = json.loads(line)
    row["started_at"] = started
    return row


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--modes", default=",".join(SUPPORTED_MODES))
    parser.add_argument("--threads", default="1,2,4,8")
    parser.add_argument("--budgets", default="400")
    parser.add_argument("--fixed-depths", default="6,8")
    parser.add_argument("--scenarios", default=",".join(DUEL_SCENARIOS))
    parser.add_argument("--runs", type=int, default=15)
    parser.add_argument("--warmup", type=int, default=3)
    parser.add_argument("--out", type=Path, default=Path("exports/minimax_parallel/results.jsonl"))
    parser.add_argument("--quick", action="store_true")
    parser.add_argument("--run-single-case-json", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--single-scenario", help=argparse.SUPPRESS)
    parser.add_argument("--single-mode", help=argparse.SUPPRESS)
    parser.add_argument("--single-threads", type=int, help=argparse.SUPPRESS)
    parser.add_argument("--single-budget-ms", type=int, help=argparse.SUPPRESS)
    parser.add_argument("--single-fixed-depth", type=int, help=argparse.SUPPRESS)
    args = parser.parse_args()
    _parse_args(parser, args)

    started = time.strftime("%Y-%m-%dT%H:%M:%S%z")
    if args.run_single_case_json:
        row = run_case(
            args.scenarios[0],
            args.modes[0],
            args.threads[0],
            args.budgets[0],
            args.fixed_depths[0],
            args.runs,
            args.warmup,
        )
        row["started_at"] = started
        print(json.dumps(row, sort_keys=True), flush=True)
        return 0

    args.out.parent.mkdir(parents=True, exist_ok=True)
    tmp_path = args.out.with_name(f".{args.out.name}.tmp.{os.getpid()}")
    with tmp_path.open("w", encoding="utf-8") as out:
        for scenario_name in args.scenarios:
            for mode in args.modes:
                for threads in args.threads:
                    for budget_ms in args.budgets:
                        for fixed_depth in args.fixed_depths:
                            row = run_single_case_child(
                                scenario_name,
                                mode,
                                threads,
                                budget_ms,
                                fixed_depth,
                                args.runs,
                                args.warmup,
                                started,
                            )
                            line = json.dumps(row, sort_keys=True)
                            out.write(line + "\n")
                            out.flush()
                            print(line, flush=True)

    tmp_path.replace(args.out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
