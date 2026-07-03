from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


Key = tuple[str, int, int]


def load(path: Path) -> dict[Key, dict[str, Any]]:
    rows: dict[Key, dict[str, Any]] = {}
    with path.open(encoding="utf-8") as source:
        for line in source:
            if not line.strip():
                continue
            row = json.loads(line)
            key = (str(row["scenario"]), int(row["budget_ms"]), int(row["fixed_depth"]))
            if key in rows:
                raise ValueError(f"duplicate benchmark row in {path}: {key}")
            rows[key] = row
    if not rows:
        raise ValueError(f"benchmark file is empty: {path}")
    return rows


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("baseline", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--max-p50-regression", type=float, default=1.08)
    args = parser.parse_args()

    try:
        baseline = load(args.baseline)
        candidate = load(args.candidate)
    except ValueError as error:
        print(error)
        return 1
    failed = False

    for key in sorted(baseline):
        baseline_row = baseline[key]
        candidate_row = candidate.get(key)
        if candidate_row is None:
            print(f"missing candidate row: scenario={key[0]} budget_ms={key[1]} fixed_depth={key[2]}")
            failed = True
            continue

        before_ms = float(baseline_row["elapsed_ms_p50"])
        after_ms = float(candidate_row["elapsed_ms_p50"])
        ratio = after_ms / max(before_ms, 0.001)
        before_depth = float(baseline_row["completed_depth"])
        after_depth = float(candidate_row["completed_depth"])

        print(
            "scenario={scenario} budget_ms={budget_ms} fixed_depth={fixed_depth} "
            "elapsed_p50={before_ms:.3f}->{after_ms:.3f} ratio={ratio:.3f} "
            "depth={before_depth:g}->{after_depth:g}".format(
                scenario=key[0],
                budget_ms=key[1],
                fixed_depth=key[2],
                before_ms=before_ms,
                after_ms=after_ms,
                ratio=ratio,
                before_depth=before_depth,
                after_depth=after_depth,
            )
        )

        if key[2] == 0:
            if after_depth < before_depth:
                print(
                    "budget completed depth regressed: scenario={scenario} budget_ms={budget_ms} "
                    "fixed_depth={fixed_depth} {before}->{after}".format(
                        scenario=key[0],
                        budget_ms=key[1],
                        fixed_depth=key[2],
                        before=before_depth,
                        after=after_depth,
                    )
                )
                failed = True
            elif ratio > args.max_p50_regression:
                failed = True
        candidate_move_counts = candidate_row.get("move_counts")
        if key[2] > 0 and isinstance(candidate_move_counts, dict) and len(candidate_move_counts) > 1:
            print(
                "fixed-depth move unstable: scenario={scenario} budget_ms={budget_ms} "
                "fixed_depth={fixed_depth} moves={moves}".format(
                    scenario=key[0],
                    budget_ms=key[1],
                    fixed_depth=key[2],
                    moves=candidate_move_counts,
                )
            )
            failed = True
        if key[2] > 0 and candidate_row.get("move") != baseline_row.get("move"):
            print(
                "fixed-depth move changed: scenario={scenario} budget_ms={budget_ms} "
                "fixed_depth={fixed_depth} {before}->{after}".format(
                    scenario=key[0],
                    budget_ms=key[1],
                    fixed_depth=key[2],
                    before=baseline_row.get("move"),
                    after=candidate_row.get("move"),
                )
            )
            failed = True

    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
