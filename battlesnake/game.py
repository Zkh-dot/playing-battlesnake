"""C-backed Battlesnake board datatypes and API conversion helpers."""

from __future__ import annotations

from battlesnake.battlesnake_native import Board, Coord, Snake
from battlesnake.types import GameState


MOVE_DELTAS: dict[str, tuple[int, int]] = {
    "up": (0, 1),
    "down": (0, -1),
    "left": (-1, 0),
    "right": (1, 0),
}


def board_from_game_state(state: GameState) -> Board:
    """Build a native C-backed board snapshot from a Battlesnake API payload."""

    settings = state.game.ruleset.settings
    hazard_damage = int(settings.get("hazardDamagePerTurn", 15))
    return Board(
        width=state.board.width,
        height=state.board.height,
        snakes={snake.id: snake for snake in state.board.snakes},
        food=state.board.food,
        hazards=state.board.hazards,
        ruleset_name=state.game.ruleset.name,
        hazard_damage=hazard_damage,
    )


__all__ = ["Board", "Coord", "Snake", "MOVE_DELTAS", "board_from_game_state"]
