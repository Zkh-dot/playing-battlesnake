#!/usr/bin/env python3
"""Build issue-41 branching-pocket fixtures from read-only replay exports."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any


OUR_NAME = "scvnak"
REPO_ROOT = Path(__file__).resolve().parents[1]
EXPORT_DIR = Path(
    "/home/sergei-scv/temp/playing-battlesnake/exports/zkh-dot_lost_games"
)
OUTPUT_PATH = REPO_ROOT / "tests" / "fixtures" / "issue_41_branching_pocket_positions.json"
POSITIONS = [
    ("7351410a-0ddf-4889-9f23-b66b2ef76c2f", 169, "down"),
    ("74f38216-24fa-472c-ba98-66282577f624", 439, "right"),
    ("0188bbac-32b6-4069-8bee-d24565c1cdd4", 288, "left"),
]


def _coord(raw: dict[str, Any]) -> list[int]:
    return [int(raw.get("X", raw.get("x"))), int(raw.get("Y", raw.get("y")))]


def _frame(export: dict[str, Any], game_id: str, turn: int) -> dict[str, Any]:
    try:
        return next(frame for frame in export["frames"] if int(frame["Turn"]) == turn)
    except StopIteration as error:
        raise RuntimeError(f"{game_id}: missing turn {turn}") from error


def _position(game_id: str, turn: int, bad_move: str) -> dict[str, Any]:
    source = EXPORT_DIR / f"{game_id}.json"
    export = json.loads(source.read_text(encoding="utf-8"))
    game = export["game"]
    frame = _frame(export, game_id, turn)
    alive = [snake for snake in frame["Snakes"] if snake.get("Death") is None]
    try:
        ours = next(snake for snake in alive if snake.get("Name") == OUR_NAME)
    except StopIteration as error:
        raise RuntimeError(f"{game_id} T{turn}: could not find live {OUR_NAME}") from error

    ruleset = game.get("Ruleset") or {}
    return {
        "evidence": {"game_id": game_id, "turn": turn, "recorded_bad_move": bad_move},
        "snake_id": str(ours["ID"]),
        "width": int(game["Width"]),
        "height": int(game["Height"]),
        "ruleset_name": str(ruleset.get("name", game.get("RulesetName", "standard"))),
        "hazard_damage": int(ruleset.get("hazardDamagePerTurn", 15)),
        "snakes": [
            {
                "id": str(snake["ID"]),
                "name": str(snake["Name"]),
                "health": int(snake["Health"]),
                "body": [_coord(coord) for coord in snake["Body"]],
            }
            for snake in alive
        ],
        "food": [_coord(coord) for coord in frame.get("Food", [])],
        "hazards": [_coord(coord) for coord in frame.get("Hazards", [])],
    }


def main() -> int:
    positions = [_position(*spec) for spec in POSITIONS]
    OUTPUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT_PATH.write_text(json.dumps({"positions": positions}, indent=1) + "\n", encoding="utf-8")
    print(f"wrote {len(positions)} positions to {OUTPUT_PATH}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
