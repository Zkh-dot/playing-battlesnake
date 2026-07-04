from __future__ import annotations

import hashlib
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Iterator

from battlesnake.battlesnake_native import Board, Coord, Snake


Move = str
Point = tuple[int, int]


@dataclass(frozen=True)
class ReplaySample:
    game_id: str
    split: str
    turn: int
    snake_id: str
    snake_name: str
    target_move: Move
    board: Board


def infer_move(previous_head: Point, next_head: Point) -> Move | None:
    dx = next_head[0] - previous_head[0]
    dy = next_head[1] - previous_head[1]
    if dx == 0 and dy == 1:
        return "up"
    if dx == 0 and dy == -1:
        return "down"
    if dx == -1 and dy == 0:
        return "left"
    if dx == 1 and dy == 0:
        return "right"
    return None


def deterministic_split(game_id: str) -> str:
    digest = hashlib.sha256(game_id.encode("utf-8")).digest()
    bucket = int.from_bytes(digest[:8], "big") % 100
    if bucket < 70:
        return "train"
    if bucket < 85:
        return "validation"
    return "test"


def _coord(raw: dict[str, int]) -> Coord:
    return Coord(int(raw["X"]), int(raw["Y"]))


def _point(raw: dict[str, int]) -> Point:
    return int(raw["X"]), int(raw["Y"])


def _snake(raw: dict[str, object]) -> Snake:
    body = [_coord(item) for item in raw.get("Body", [])]
    snake_id = str(raw["ID"])
    snake_name = str(raw.get("Name") or snake_id)
    health = int(raw.get("Health", 100))
    return Snake(snake_id, snake_name, health, body, length=len(body))


def _hazard_damage(game: dict[str, object]) -> int:
    ruleset = game.get("Ruleset", {})
    if isinstance(ruleset, dict):
        settings_value = ruleset.get("settings")
        if isinstance(settings_value, dict) and "hazardDamagePerTurn" in settings_value:
            return int(settings_value["hazardDamagePerTurn"])
        if "hazardDamagePerTurn" in ruleset:
            return int(ruleset["hazardDamagePerTurn"])
    return 15


def _ruleset_name(game: dict[str, object]) -> str:
    if "RulesetName" in game:
        return str(game["RulesetName"])
    ruleset = game.get("Ruleset", {})
    if isinstance(ruleset, dict) and "name" in ruleset:
        return str(ruleset["name"])
    return "standard"


def _board_from_frame(game: dict[str, object], frame: dict[str, object]) -> Board:
    width = int(game["Width"])
    height = int(game["Height"])
    snakes = {_snake(raw).id: _snake(raw) for raw in frame.get("Snakes", [])}
    food = [_coord(raw) for raw in frame.get("Food", [])]
    hazards = [_coord(raw) for raw in frame.get("Hazards", [])]
    return Board(
        width=width,
        height=height,
        snakes=snakes,
        food=food,
        hazards=hazards,
        ruleset_name=_ruleset_name(game),
        hazard_damage=_hazard_damage(game),
    )


def _heads_by_snake(frame: dict[str, object]) -> dict[str, Point]:
    heads: dict[str, Point] = {}
    for raw_snake in frame.get("Snakes", []):
        snake_id = str(raw_snake["ID"])
        body = raw_snake.get("Body", [])
        if body:
            heads[snake_id] = _point(body[0])
    return heads


def iter_replay_samples(paths: Iterable[Path]) -> Iterator[ReplaySample]:
    for path in paths:
        export = json.loads(path.read_text())
        game = export["game"]
        game_id = str(export.get("game_id") or game.get("ID", "unknown"))
        split = deterministic_split(game_id)
        frames = export.get("frames", [])
        for index in range(len(frames) - 1):
            frame = frames[index]
            next_frame = frames[index + 1]
            next_heads = _heads_by_snake(next_frame)
            board = _board_from_frame(game, frame)
            for raw_snake in frame.get("Snakes", []):
                snake_id = str(raw_snake["ID"])
                body = raw_snake.get("Body", [])
                if not body or snake_id not in next_heads:
                    continue
                move = infer_move(_point(body[0]), next_heads[snake_id])
                if move is None:
                    continue
                yield ReplaySample(
                    game_id=game_id,
                    split=split,
                    turn=int(frame["Turn"]),
                    snake_id=snake_id,
                    snake_name=str(raw_snake.get("Name") or snake_id),
                    target_move=move,
                    board=board,
                )


def export_paths(root: Path) -> list[Path]:
    if root.is_file() and root.suffix == ".json":
        return [root]
    return sorted(path for path in root.rglob("*.json") if path.is_file())
