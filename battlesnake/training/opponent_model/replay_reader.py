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


def _to_int(raw: Any) -> int | None:
    try:
        return int(raw)
    except (TypeError, ValueError):
        return None


def _point(raw: Any) -> tuple[int, int] | None:
    if not isinstance(raw, dict):
        return None
    x = _to_int(raw.get("X"))
    y = _to_int(raw.get("Y"))
    if x is None or y is None:
        return None
    return x, y


def _coord(raw: Any) -> Coord | None:
    point = _point(raw)
    if point is None:
        return None
    return Coord(point[0], point[1])


def _coords(raw_items: Any) -> list[Coord] | None:
    if not isinstance(raw_items, list):
        return None
    coords = []
    for raw in raw_items:
        coord = _coord(raw)
        if coord is None:
            return None
        coords.append(coord)
    return coords


def _snake(raw: dict[str, Any]) -> Snake | None:
    body = _coords(raw.get("Body", []))
    if body is None:
        return None
    if "ID" not in raw:
        return None
    health = _to_int(raw.get("Health", 100))
    if health is None:
        return None
    snake_id = str(raw["ID"])
    snake_name = str(raw.get("Name") or snake_id)
    return Snake(snake_id, snake_name, health, body, length=len(body))


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
            hazard_damage = _to_int(ruleset["hazardDamagePerTurn"])
            return hazard_damage if hazard_damage is not None else 15
        settings = ruleset.get("settings")
        if isinstance(settings, dict) and "hazardDamagePerTurn" in settings:
            hazard_damage = _to_int(settings["hazardDamagePerTurn"])
            return hazard_damage if hazard_damage is not None else 15
    return 15


def _snakes(frame: dict[str, Any]) -> list[Any]:
    snakes = frame.get("Snakes", [])
    if not isinstance(snakes, list):
        return []
    return snakes


def _alive_snakes(frame: dict[str, Any]) -> list[dict[str, Any]]:
    snakes = []
    for raw in _snakes(frame):
        if not isinstance(raw, dict):
            continue
        if raw.get("Death"):
            continue
        if raw.get("IsEnvironment"):
            continue
        if "ID" not in raw:
            continue
        body = raw.get("Body")
        if isinstance(body, list) and body:
            snakes.append(raw)
    return snakes


def _heads_by_id(frame: dict[str, Any]) -> dict[str, tuple[int, int]]:
    heads = {}
    for raw in _snakes(frame):
        if not isinstance(raw, dict):
            continue
        if raw.get("IsEnvironment"):
            continue
        if "ID" not in raw:
            continue
        body = raw.get("Body")
        if not isinstance(body, list) or not body:
            continue
        point = _point(body[0])
        if point is not None:
            heads[str(raw["ID"])] = point
    return heads


def _board_dimensions(game: dict[str, Any]) -> tuple[int, int] | None:
    width = _to_int(game.get("Width"))
    height = _to_int(game.get("Height"))
    if width is None or height is None:
        return None
    return width, height


def _valid_coords(raw_items: Any) -> list[Coord]:
    if not isinstance(raw_items, list):
        return []
    return [coord for raw in raw_items if (coord := _coord(raw)) is not None]


def _board_from_frame(game: dict[str, Any], frame: dict[str, Any]) -> Board | None:
    dimensions = _board_dimensions(game)
    if dimensions is None:
        return None
    snakes = []
    for raw in _alive_snakes(frame):
        snake = _snake(raw)
        if snake is None:
            return None
        snakes.append(snake)
    width, height = dimensions
    return Board(
        width=width,
        height=height,
        snakes={snake.id: snake for snake in snakes},
        food=_valid_coords(frame.get("Food", [])),
        hazards=_valid_coords(frame.get("Hazards", [])),
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
    if _board_dimensions(game) is None:
        return

    game_id = str(export.get("game_id") or game.get("ID") or archive_name.removesuffix(".json"))
    split = deterministic_split(game_id)
    for frame, next_frame in zip(frames, frames[1:]):
        if not isinstance(frame, dict) or not isinstance(next_frame, dict):
            continue
        next_heads = _heads_by_id(next_frame)
        board = _board_from_frame(game, frame)
        if board is None:
            continue
        turn = _to_int(frame.get("Turn", 0))
        if turn is None:
            continue
        alive = _alive_snakes(frame)
        for raw_snake in alive:
            snake_id = str(raw_snake["ID"])
            body = raw_snake.get("Body", [])
            if not body or snake_id not in next_heads:
                continue
            head = _point(body[0])
            if head is None:
                continue
            move = infer_move(head, next_heads[snake_id])
            if move is None:
                continue
            snake_name = str(raw_snake.get("Name") or snake_id)
            meta = ranks_by_display.get(snake_name)
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
