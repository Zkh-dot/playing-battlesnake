from __future__ import annotations

import argparse
import json
from collections import defaultdict
from pathlib import Path
from typing import Any

LIVE_LATENCY_P95_MS = 450.0


def load_rows(path: Path) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8") as file:
        for line in file:
            stripped = line.strip()
            if stripped:
                rows.append(json.loads(stripped))
    return rows


def key_without_mode(row: dict[str, Any]) -> tuple[str, int, int, int]:
    return (
        str(row["scenario"]),
        int(row["threads"]),
        int(row["budget_ms"]),
        int(row["fixed_depth"]),
    )


def group_key(row: dict[str, Any]) -> tuple[str, int, int, int]:
    return (
        str(row["mode"]),
        int(row["threads"]),
        int(row["budget_ms"]),
        int(row["fixed_depth"]),
    )


def group_label(group: tuple[str, int, int, int]) -> str:
    _, threads, budget_ms, fixed_depth = group
    return f"{threads}:{budget_ms}:{fixed_depth}"


def _median(values: list[float]) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    midpoint = len(ordered) // 2
    if len(ordered) % 2:
        return ordered[midpoint]
    return (ordered[midpoint - 1] + ordered[midpoint]) / 2.0


def _empty_group() -> dict[str, Any]:
    return {
        "wins": set(),
        "regressions": 0,
        "latency_failures": 0,
        "correctness_failures": 0,
        "missing_baselines": 0,
        "metrics": [],
        "metric_name": "",
    }


def has_correctness_failure(
    row: dict[str, Any],
    serial: dict[str, Any],
    score_tolerance: float,
) -> bool:
    if str(row["last_move"]) != str(serial["last_move"]):
        return True

    fixed_depth = int(row["fixed_depth"])
    if fixed_depth > 0 and float(row["completed_depth_p50"]) != float(serial["completed_depth_p50"]):
        return True

    return abs(float(row["score_p50"]) - float(serial["score_p50"])) > score_tolerance


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--results", type=Path, required=True)
    parser.add_argument("--min-speedup", type=float, default=1.05)
    parser.add_argument("--score-tolerance", type=float, default=1e-6)
    args = parser.parse_args()

    rows = load_rows(args.results)
    serial_baselines: dict[tuple[str, int, int, int], dict[str, Any]] = {}
    groups: dict[tuple[str, int, int, int], dict[str, Any]] = defaultdict(_empty_group)
    candidate_count = 0
    missing_baseline_count = 0

    for row in rows:
        if row["mode"] == "serial":
            serial_baselines[key_without_mode(row)] = row

    for row in rows:
        if row["mode"] == "serial":
            continue

        candidate_count += 1
        group = group_key(row)
        stats = groups[group]
        serial = serial_baselines.get(key_without_mode(row))
        if serial is None:
            stats["missing_baselines"] += 1
            missing_baseline_count += 1
            continue

        scenario = str(row["scenario"])
        fixed_depth = int(row["fixed_depth"])
        candidate_elapsed_p95 = float(row["elapsed_ms_p95"])
        if has_correctness_failure(row, serial, args.score_tolerance):
            stats["correctness_failures"] += 1

        if fixed_depth > 0:
            candidate_elapsed_p50 = float(row["elapsed_ms_p50"])
            speedup = (
                float(serial["elapsed_ms_p50"]) / candidate_elapsed_p50
                if candidate_elapsed_p50 > 0.0
                else 0.0
            )
            stats["metric_name"] = "speedup"
            stats["metrics"].append(speedup)
            if speedup >= args.min_speedup:
                stats["wins"].add(scenario)
            if speedup < 0.90:
                stats["regressions"] += 1
        else:
            serial_depth = float(serial["completed_depth_p50"])
            candidate_depth = float(row["completed_depth_p50"])
            depth_delta = candidate_depth - serial_depth
            stats["metric_name"] = "depth_delta"
            stats["metrics"].append(depth_delta)
            if candidate_elapsed_p95 > LIVE_LATENCY_P95_MS:
                stats["latency_failures"] += 1
            if candidate_depth > serial_depth or (
                candidate_depth == serial_depth and candidate_elapsed_p95 <= LIVE_LATENCY_P95_MS
            ):
                stats["wins"].add(scenario)
            if candidate_depth < serial_depth:
                stats["regressions"] += 1

    for group in sorted(groups):
        mode, _, _, fixed_depth = group
        stats = groups[group]
        wins = len(stats["wins"])
        regressions = int(stats["regressions"])
        latency_failures = int(stats["latency_failures"])
        correctness_failures = int(stats["correctness_failures"])
        missing_baselines = int(stats["missing_baselines"])
        metrics = [float(value) for value in stats["metrics"]]
        metric_name = str(stats["metric_name"] or ("speedup" if fixed_depth > 0 else "depth_delta"))
        decision = (
            "keep"
            if wins >= 4
            and regressions == 0
            and latency_failures == 0
            and correctness_failures == 0
            and missing_baselines == 0
            else "revert"
        )
        print(
            f"mode={mode} decision={decision} wins={wins} "
            f"regressions={regressions} latency_failures={latency_failures} "
            f"correctness_failures={correctness_failures} missing_baselines={missing_baselines} "
            f"best_{metric_name}={max(metrics, default=0.0):.3f} "
            f"median_{metric_name}={_median(metrics):.3f} best_group={group_label(group)}"
        )

    if candidate_count and missing_baseline_count:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
