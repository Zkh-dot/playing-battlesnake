#!/usr/bin/env python3
"""Build issue-44 corridor-guard fixtures from read-only replay exports."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


OUR_NAME = "scvnak"
REPO_ROOT = Path(__file__).resolve().parents[1]
EXPORT_DIR = REPO_ROOT / "exports" / "zkh-dot_lost_games"
OUTPUT_PATH = (
    REPO_ROOT / "tests" / "fixtures" / "issue_44_corridor_guard_positions.json"
)
POSITIONS = [
    ("091dc137-5a89-4471-b646-5540de694fe9", 290, "right", "left", None),
    ("1985bf57-7fb4-4842-8ac0-b8900d31e1be", 424, "down", "up", "up"),
    ("74f38216-24fa-472c-ba98-66282577f624", 317, "left", "right", None),
    ("7ea501e7-db9f-40e3-9a72-d4efb77c5d59", 187, "right", "down", None),
    ("f5d7c374-6c5c-4459-9bae-b897b0604165", 284, "up", "down", None),
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


def _position(
    game_id: str,
    turn: int,
    historical_guard_move: str,
    reported_alternative_move: str,
    expected_authoritative_move: str | None,
    export_dir: Path = EXPORT_DIR,
) -> dict[str, Any]:
    export = json.loads((export_dir / f"{game_id}.json").read_text(encoding="utf-8"))
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
    if recorded_move != historical_guard_move:
        raise RuntimeError(
            f"{game_id} T{turn}: expected recorded {historical_guard_move}, got {recorded_move}"
        )

    evidence: dict[str, Any] = {
        "game_id": game_id,
        "turn": turn,
        "historical_guard_move": historical_guard_move,
        "reported_alternative_move": reported_alternative_move,
    }
    if expected_authoritative_move is not None:
        evidence["expected_authoritative_move"] = expected_authoritative_move

    ruleset = game.get("Ruleset") or {}
    return {
        "evidence": evidence,
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
