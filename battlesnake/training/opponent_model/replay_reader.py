from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Iterator

from battlesnake.battlesnake_native import Board, Coord, Snake
from battlesnake.training.opponent_model.archive_loader import PlayerMeta
from battlesnake.training.opponent_model.schema import MoveObservation, deterministic_split, infer_move


@dataclass(frozen=True)
class BoardObservation:
    observation: MoveObservation
    board: Board


def _coord(raw: dict[str, Any]) -> Coord:
    return Coord(int(raw["X"]), int(raw["Y"]))


def _point(raw: dict[str, Any]) -> tuple[int, int]:
    return int(raw["X"]), int(raw["Y"])


def _snake(raw: dict[str, Any]) -> Snake:
    body = [_coord(item) for item in raw.get("Body", [])]
    snake_id = str(raw["ID"])
    snake_name = str(raw.get("Name") or snake_id)
    return Snake(snake_id, snake_name, int(raw.get("Health", 100)), body, length=len(body))


def _ruleset_name(game: dict[str, Any]) -> str:
    if "RulesetName" in game:
        return str(game["RulesetName"]).lower()
    ruleset = game.get("Ruleset", {})
    if isinstance(ruleset, dict):
        return str(ruleset.get("name", "standard")).lower()
    return "standard"


def _hazard_damage(game: dict[str, Any]) -> int:
    ruleset = game.get("Ruleset", {})
    if isinstance(ruleset, dict):
        if "hazardDamagePerTurn" in ruleset:
            return int(ruleset["hazardDamagePerTurn"])
        settings = ruleset.get("settings")
        if isinstance(settings, dict) and "hazardDamagePerTurn" in settings:
            return int(settings["hazardDamagePerTurn"])
    return 15


def _alive_snakes(frame: dict[str, Any]) -> list[dict[str, Any]]:
    snakes = []
    for raw in frame.get("Snakes", []):
        if raw.get("Death"):
            continue
        if raw.get("IsEnvironment"):
            continue
        if raw.get("Body"):
            snakes.append(raw)
    return snakes


def _heads_by_id(frame: dict[str, Any]) -> dict[str, tuple[int, int]]:
    heads = {}
    for raw in frame.get("Snakes", []):
        if raw.get("IsEnvironment"):
            continue
        body = raw.get("Body", [])
        if body:
            heads[str(raw["ID"])] = _point(body[0])
    return heads


def _board_from_frame(game: dict[str, Any], frame: dict[str, Any]) -> Board:
    snakes = [_snake(raw) for raw in _alive_snakes(frame)]
    return Board(
        width=int(game["Width"]),
        height=int(game["Height"]),
        snakes={snake.id: snake for snake in snakes},
        food=[_coord(raw) for raw in frame.get("Food", [])],
        hazards=[_coord(raw) for raw in frame.get("Hazards", [])],
        ruleset_name=_ruleset_name(game),
        hazard_damage=_hazard_damage(game),
    )


def iter_move_observations(
    archive_name: str,
    export: dict[str, Any],
    ranks_by_display: dict[str, PlayerMeta],
) -> Iterator[BoardObservation]:
    game = export.get("game", {})
    frames = export.get("frames", [])
    if not isinstance(game, dict) or not isinstance(frames, list) or len(frames) < 2:
        return
    if _ruleset_name(game) != "standard":
        return

    game_id = str(export.get("game_id") or game.get("ID") or archive_name.removesuffix(".json"))
    split = deterministic_split(game_id)
    for frame, next_frame in zip(frames, frames[1:]):
        if not isinstance(frame, dict) or not isinstance(next_frame, dict):
            continue
        next_heads = _heads_by_id(next_frame)
        board = _board_from_frame(game, frame)
        alive = _alive_snakes(frame)
        for raw_snake in alive:
            snake_id = str(raw_snake["ID"])
            body = raw_snake.get("Body", [])
            if not body or snake_id not in next_heads:
                continue
            move = infer_move(_point(body[0]), next_heads[snake_id])
            if move is None:
                continue
            snake_name = str(raw_snake.get("Name") or snake_id)
            meta = ranks_by_display.get(snake_name)
            turn = int(frame.get("Turn", 0))
            yield BoardObservation(
                observation=MoveObservation(
                    observation_id=f"{game_id}:{turn}:{snake_id}",
                    game_id=game_id,
                    split=split,
                    turn=turn,
                    snake_id=snake_id,
                    snake_name=snake_name,
                    snake_rank=meta.rank if meta else None,
                    target_move=move,
                    board_width=board.width,
                    board_height=board.height,
                    alive_snakes=len(alive),
                ),
                board=board,
            )
