#!/usr/bin/env python3
"""Compare named duel profiles on compact diagnostic replay positions."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from battlesnake.battlesnake_native import Board, Coord, Snake, minimax_diagnostics
from tools.check_duel_structural_policy import _complete_opponent_replies, audit_diagnostics
from tools.tuning.compare_weights_matches import _is_structural_risk
from tools.tuning.duel_weight_profiles import load_profile


DEFAULT_PROFILES = (
    ROOT / "configs/evaluation_weights/default.json",
    ROOT / "configs/evaluation_weights/tuned-opponent-pressure.json",
)


def _board(raw: dict[str, object]) -> Board:
    snakes = [
        Snake(str(snake["id"]), str(snake["name"]), int(snake["health"]),
              [Coord(int(point[0]), int(point[1])) for point in snake["body"]])
        for snake in raw["snakes"]
    ]
    points = lambda name: [Coord(int(point[0]), int(point[1])) for point in raw[name]]
    return Board(int(raw["width"]), int(raw["height"]), snakes, points("food"), points("hazards"),
                 str(raw["ruleset_name"]), int(raw["hazard_damage"]))


def _root_comparison(diagnostics: dict[str, object]) -> dict[str, object]:
    fields = ("allowed", "rejection_reason", "structural_proof", "relaxed_static_capacity",
              "post_move_length", "minimax_score", "minimax_outcome", "minimax_bound")
    return {
        move: {field: candidate.get(field) for field in fields}
        for move, candidate in diagnostics["root_candidates"].items()
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixtures", type=Path, required=True)
    parser.add_argument("--profile", action="append", type=Path, dest="profiles")
    parser.add_argument("--budget-ms", type=int, required=True)
    parser.add_argument("--repeats", type=int, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.budget_ms <= 0 or args.repeats <= 0:
        parser.error("budget-ms and repeats must be positive")
    raw = json.loads(args.fixtures.read_text())
    if raw.get("schema_version") != 1 or len(raw.get("positions", [])) != 4:
        raise ValueError("fixture must contain exactly four schema-version-1 positions")
    profiles = [load_profile(path) for path in (args.profiles or DEFAULT_PROFILES)]
    if len(profiles) != 2 or len({profile.identifier for profile in profiles}) != 2:
        raise ValueError("exactly two distinct named profiles are required")
    records: list[dict[str, object]] = []
    for position in raw["positions"]:
        board = _board(position["board"])
        for profile in profiles:
            for repeat in range(args.repeats):
                base = {
                    "game_id": position["game_id"], "turn": position["turn"],
                    "recorded_move": position["recorded_move"], "snake_id": position["snake_id"],
                    "profile": f"{profile.name}@{profile.version}", "profile_sha256": profile.sha256,
                    "repeat": repeat,
                }
                try:
                    diagnostics = minimax_diagnostics(
                        board, position["snake_id"], time_budget_ms=args.budget_ms,
                        enable_tt=True, enable_move_ordering=True, enable_make_unmake=True,
                        weights=dict(profile.weights),
                    )
                    audit_input = dict(diagnostics)
                    audit_input["complete_opponent_replies"] = _complete_opponent_replies(board, position["snake_id"])
                    audit = audit_diagnostics(audit_input)
                    selected = diagnostics["root_candidates"][diagnostics["move"]]
                    records.append({
                        **base, "move": diagnostics["move"], "depth": diagnostics["completed_depth"],
                        "timed_out": diagnostics["timed_out"], "elapsed_ms": diagnostics["elapsed_ms"],
                        "selected_structural_proof": selected["structural_proof"],
                        "root_comparison": _root_comparison(diagnostics),
                        "structural_risk": _is_structural_risk(diagnostics),
                        "policy_violation": audit.violation, "error": None,
                    })
                except Exception as exc:
                    records.append({
                        **base, "move": None, "depth": None, "timed_out": False, "elapsed_ms": None,
                        "selected_structural_proof": None, "root_comparison": {},
                        "structural_risk": False, "policy_violation": False,
                        "error": f"{type(exc).__name__}: {exc}",
                    })
    payload = {
        "schema_version": 1,
        "settings": {"budget_ms": args.budget_ms, "repeats": args.repeats},
        "profiles": [{"identifier": f"{p.name}@{p.version}", "sha256": p.sha256} for p in profiles],
        "records": records,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")
    errors = sum(record["error"] is not None for record in records)
    print(json.dumps({"records": len(records), "errors": errors}, sort_keys=True))
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
