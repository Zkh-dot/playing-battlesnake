#!/usr/bin/env python3
"""Build issue-41 branching-pocket fixtures from read-only replay exports."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


OUR_NAME = "scvnak"
REPO_ROOT = Path(__file__).resolve().parents[1]
EXPORT_DIR = REPO_ROOT / "exports" / "zkh-dot_lost_games"
OUTPUT_PATH = REPO_ROOT / "tests" / "fixtures" / "issue_41_branching_pocket_positions.json"
POSITIONS = [
    ("7351410a-0ddf-4889-9f23-b66b2ef76c2f", 169),
    ("74f38216-24fa-472c-ba98-66282577f624", 439),
    ("0188bbac-32b6-4069-8bee-d24565c1cdd4", 288),
]


def _coord(raw: dict[str, Any]) -> list[int]:
    return [int(raw.get("X", raw.get("x"))), int(raw.get("Y", raw.get("y")))]


def _frame(export: dict[str, Any], game_id: str, turn: int) -> dict[str, Any]:
    try:
        return next(frame for frame in export["frames"] if int(frame["Turn"]) == turn)
    except StopIteration as error:
        raise RuntimeError(f"{game_id}: missing turn {turn}") from error


def _alive_snake(frame: dict[str, Any], snake_id: str) -> dict[str, Any]:
    matches = [
        snake
        for snake in frame.get("Snakes", [])
        if str(snake.get("ID")) == snake_id and snake.get("Death") is None
    ]
    if len(matches) != 1 or not matches[0].get("Body"):
        raise RuntimeError(f"T{frame.get('Turn')}: missing live snake {snake_id}")
    return matches[0]


def _recorded_move(
    current: dict[str, Any], following: dict[str, Any], snake_id: str
) -> str:
    current_turn = int(current["Turn"])
    if int(following["Turn"]) != current_turn + 1:
        raise RuntimeError(f"expected adjacent frames T{current_turn} and T{current_turn + 1}")
    old_head = _coord(_alive_snake(current, snake_id)["Body"][0])
    new_head = _coord(_alive_snake(following, snake_id)["Body"][0])
    delta = (new_head[0] - old_head[0], new_head[1] - old_head[1])
    moves = {(0, 1): "up", (0, -1): "down", (-1, 0): "left", (1, 0): "right"}
    if delta not in moves:
        raise RuntimeError(f"T{current_turn}: non-adjacent head transition {old_head} -> {new_head}")
    return moves[delta]


def _position(game_id: str, turn: int, export_dir: Path = EXPORT_DIR) -> dict[str, Any]:
    source = export_dir / f"{game_id}.json"
    export = json.loads(source.read_text(encoding="utf-8"))
    game = export["game"]
    frame = _frame(export, game_id, turn)
    following = _frame(export, game_id, turn + 1)
    alive = [snake for snake in frame["Snakes"] if snake.get("Death") is None]
    try:
        ours = next(snake for snake in alive if snake.get("Name") == OUR_NAME)
    except StopIteration as error:
        raise RuntimeError(f"{game_id} T{turn}: could not find live {OUR_NAME}") from error
    snake_id = str(ours["ID"])
    recorded_move = _recorded_move(frame, following, snake_id)

    ruleset = game.get("Ruleset") or {}
    return {
        "evidence": {"game_id": game_id, "turn": turn, "recorded_bad_move": recorded_move},
        "snake_id": snake_id,
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


def main(export_dir: Path = EXPORT_DIR, output_path: Path = OUTPUT_PATH) -> int:
    positions = [_position(*spec, export_dir=export_dir) for spec in POSITIONS]
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(
        json.dumps({"positions": positions}, indent=1) + "\n", encoding="utf-8"
    )
    print(f"wrote {len(positions)} positions to {output_path}")
    return 0


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--export-dir", type=Path, default=EXPORT_DIR)
    parser.add_argument("--output", type=Path, default=OUTPUT_PATH)
    return parser.parse_args()


if __name__ == "__main__":
    args = _parse_args()
    raise SystemExit(main(export_dir=args.export_dir, output_path=args.output))
