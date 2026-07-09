#!/usr/bin/env python3
"""Build issue-36 endgame fixtures from exported Battlesnake replays."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any


OUR_NAME = "scvnak"
REPO_ROOT = Path(__file__).resolve().parents[1]
EXPORT_DIR = REPO_ROOT / "exports" / "issue_36"
OUTPUT_PATH = REPO_ROOT / "tests" / "fixtures" / "issue_36_endgame_positions.json"
REQUIRED_POSITIONS = {
    ("923544bf-4fee-4aba-bfdc-01202e911637", 322),
    ("923544bf-4fee-4aba-bfdc-01202e911637", 323),
}

CRITICAL_TURNS = [
    ("ebaca2a0-0f2a-411d-87a1-e2766d7daa50", 450, "left", "left"),
    ("ebaca2a0-0f2a-411d-87a1-e2766d7daa50", 451, "left", None),
    ("9f1b79ed-9fbf-4732-aabb-254c0fb3fd6c", 290, "left", "left"),
    ("9f1b79ed-9fbf-4732-aabb-254c0fb3fd6c", 298, "left", None),
    ("923544bf-4fee-4aba-bfdc-01202e911637", 322, "up", None),
    ("923544bf-4fee-4aba-bfdc-01202e911637", 323, "right", None),
    ("b085baae-2831-4932-af82-9410568c3bf6", 344, "left", None),
    ("b085baae-2831-4932-af82-9410568c3bf6", 350, "down", None),
    ("e1265a85-5111-42b5-9f7c-b2a0e4c5d1e7", 290, "left", None),
    ("e1265a85-5111-42b5-9f7c-b2a0e4c5d1e7", 296, "right", None),
]


def _coord(raw: dict[str, Any]) -> list[int]:
    return [int(raw.get("X", raw.get("x"))), int(raw.get("Y", raw.get("y")))]


def _ruleset_name(game: dict[str, Any]) -> str:
    ruleset = game.get("Ruleset")
    if isinstance(ruleset, dict) and ruleset.get("name") is not None:
        return str(ruleset["name"])
    if game.get("RulesetName") is not None:
        return str(game["RulesetName"])
    return "standard"


def _hazard_damage(game: dict[str, Any]) -> int:
    ruleset = game.get("Ruleset")
    if isinstance(ruleset, dict) and ruleset.get("hazardDamagePerTurn") is not None:
        return int(ruleset["hazardDamagePerTurn"])
    return 15


def _frame_by_turn(export: dict[str, Any], game_id: str, turn: int) -> dict[str, Any]:
    for frame in export.get("frames", []):
        if int(frame["Turn"]) == turn:
            return frame
    raise RuntimeError(f"{game_id}: missing requested turn {turn}")


def _our_alive_snake(frame: dict[str, Any], game_id: str, turn: int) -> dict[str, Any]:
    for snake in frame.get("Snakes", []):
        if snake.get("Name") == OUR_NAME:
            if snake.get("Death") is not None:
                raise RuntimeError(f"{game_id} T{turn}: {OUR_NAME} is not alive")
            return snake
    raise RuntimeError(f"{game_id} T{turn}: could not find snake named {OUR_NAME}")


def _position(
    export: dict[str, Any],
    game_id: str,
    turn: int,
    replayed_move: str,
    bad_move: str | None,
) -> dict[str, Any]:
    game = export.get("game") or {}
    frame = _frame_by_turn(export, game_id, turn)
    our_snake = _our_alive_snake(frame, game_id, turn)

    position: dict[str, Any] = {
        "game_id": game_id,
        "turn": turn,
        "snake_id": str(our_snake["ID"]),
        "width": int(game["Width"]),
        "height": int(game["Height"]),
        "ruleset_name": _ruleset_name(game),
        "hazard_damage": _hazard_damage(game),
        "replayed_move": replayed_move,
        "snakes": [
            {
                "id": str(snake["ID"]),
                "name": str(snake["Name"]),
                "health": int(snake["Health"]),
                "body": [_coord(coord) for coord in snake.get("Body", [])],
            }
            for snake in frame.get("Snakes", [])
            if snake.get("Death") is None
        ],
        "food": [_coord(coord) for coord in frame.get("Food", [])],
        "hazards": [_coord(coord) for coord in frame.get("Hazards", [])],
    }
    if bad_move is not None:
        position["bad_move"] = bad_move
    return position


def main() -> int:
    exports: dict[str, dict[str, Any]] = {}
    positions: list[dict[str, Any]] = []

    for game_id, turn, replayed_move, bad_move in CRITICAL_TURNS:
        if game_id not in exports:
            export_path = EXPORT_DIR / f"{game_id}.json"
            if not export_path.exists():
                print(f"skipping {game_id}: missing {export_path}")
                continue
            exports[game_id] = json.loads(export_path.read_text(encoding="utf-8"))

        positions.append(_position(exports[game_id], game_id, turn, replayed_move, bad_move))

    present = {(str(position["game_id"]), int(position["turn"])) for position in positions}
    missing_required = sorted(REQUIRED_POSITIONS - present)
    if missing_required:
        raise RuntimeError(f"missing required issue-36 positions: {missing_required}")

    OUTPUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT_PATH.write_text(json.dumps({"positions": positions}, indent=1) + "\n", encoding="utf-8")
    print(f"wrote {len(positions)} positions to {OUTPUT_PATH}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
