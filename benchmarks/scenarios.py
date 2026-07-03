from __future__ import annotations

from dataclasses import dataclass
from typing import Iterable

from battlesnake.battlesnake_native import Board, Coord, Snake


@dataclass(frozen=True)
class Scenario:
    name: str
    width: int
    height: int
    ruleset_name: str
    hazard_damage: int
    snakes: tuple[Snake, ...]
    food: tuple[Coord, ...]
    hazards: tuple[Coord, ...]
    snake_id: str = "me"


def _coords(points: Iterable[tuple[int, int]]) -> tuple[Coord, ...]:
    return tuple(Coord(x, y) for x, y in points)


def _snake(snake_id: str, body: Iterable[tuple[int, int]], health: int = 90) -> Snake:
    coords = _coords(body)
    if not coords:
        raise ValueError(f"snake {snake_id} must have a body")
    return Snake(
        id=snake_id,
        name=snake_id,
        health=health,
        body=coords,
        head=coords[0],
        length=len(coords),
    )


def _border_hazards(width: int, height: int) -> tuple[Coord, ...]:
    return _coords(
        (x, y)
        for x in range(width)
        for y in range(height)
        if x == 0 or y == 0 or x == width - 1 or y == height - 1
    )


SCENARIOS = (
    Scenario(
        name="duel_open_7x7",
        width=7,
        height=7,
        ruleset_name="standard",
        hazard_damage=0,
        snakes=(
            _snake("me", [(1, 3), (1, 2), (1, 1)], 90),
            _snake("you", [(5, 3), (5, 2), (5, 1)], 90),
        ),
        food=_coords([(3, 3), (2, 5), (4, 5)]),
        hazards=(),
    ),
    Scenario(
        name="duel_center_pressure_11x11",
        width=11,
        height=11,
        ruleset_name="standard",
        hazard_damage=0,
        snakes=(
            _snake("me", [(4, 5), (4, 4), (3, 4), (2, 4), (2, 3)], 83),
            _snake("you", [(6, 5), (6, 6), (7, 6), (8, 6), (8, 7)], 88),
        ),
        food=_coords([(5, 5), (1, 8), (9, 2), (5, 8)]),
        hazards=(),
    ),
    Scenario(
        name="duel_low_health_food_race",
        width=11,
        height=11,
        ruleset_name="standard",
        hazard_damage=0,
        snakes=(
            _snake("me", [(3, 5), (3, 4), (3, 3), (2, 3)], 14),
            _snake("you", [(7, 5), (7, 6), (7, 7), (8, 7)], 72),
        ),
        food=_coords([(5, 5), (2, 8), (8, 2)]),
        hazards=(),
    ),
    Scenario(
        name="duel_tail_chase_trap",
        width=11,
        height=11,
        ruleset_name="standard",
        hazard_damage=0,
        snakes=(
            _snake("me", [(5, 2), (5, 1), (4, 1), (3, 1), (3, 2), (3, 3)], 77),
            _snake("you", [(5, 8), (5, 9), (6, 9), (7, 9), (7, 8), (7, 7)], 80),
        ),
        food=_coords([(5, 5), (1, 1), (9, 9)]),
        hazards=(),
    ),
    Scenario(
        name="duel_corridor_choke",
        width=11,
        height=11,
        ruleset_name="standard",
        hazard_damage=0,
        snakes=(
            _snake("me", [(2, 5), (2, 4), (2, 3), (1, 3), (1, 2), (1, 1)], 90),
            _snake("you", [(8, 5), (8, 6), (8, 7), (9, 7), (9, 8), (9, 9)], 90),
        ),
        food=_coords([(5, 5), (5, 4)]),
        hazards=_coords(
            [
                (4, 0),
                (4, 1),
                (4, 2),
                (4, 3),
                (4, 7),
                (4, 8),
                (4, 9),
                (4, 10),
                (6, 0),
                (6, 1),
                (6, 2),
                (6, 3),
                (6, 7),
                (6, 8),
                (6, 9),
                (6, 10),
            ]
        ),
    ),
    Scenario(
        name="duel_late_game_long_bodies",
        width=11,
        height=11,
        ruleset_name="standard",
        hazard_damage=0,
        snakes=(
            _snake(
                "me",
                [(3, 7), (3, 6), (2, 6), (1, 6), (1, 5), (1, 4), (2, 4), (3, 4), (4, 4), (4, 3), (4, 2), (3, 2)],
                64,
            ),
            _snake(
                "you",
                [(7, 3), (7, 4), (8, 4), (9, 4), (9, 5), (9, 6), (8, 6), (7, 6), (6, 6), (6, 7), (6, 8), (7, 8)],
                68,
            ),
        ),
        food=_coords([(5, 5), (2, 9), (8, 1)]),
        hazards=(),
    ),
    Scenario(
        name="royale_hazard_ring_duel",
        width=11,
        height=11,
        ruleset_name="royale",
        hazard_damage=15,
        snakes=(
            _snake("me", [(4, 5), (4, 4), (4, 3), (3, 3)], 55),
            _snake("you", [(6, 5), (6, 6), (6, 7), (7, 7)], 60),
        ),
        food=_coords([(5, 5), (3, 5), (7, 5)]),
        hazards=_border_hazards(11, 11),
    ),
    Scenario(
        name="standard_four_snakes_dense",
        width=11,
        height=11,
        ruleset_name="standard",
        hazard_damage=0,
        snakes=(
            _snake("me", [(2, 2), (2, 1), (1, 1), (1, 0)], 82),
            _snake("north", [(5, 8), (5, 9), (4, 9), (3, 9)], 90),
            _snake("east", [(8, 5), (9, 5), (9, 4), (9, 3)], 90),
            _snake("west", [(2, 7), (1, 7), (1, 8), (1, 9)], 90),
        ),
        food=_coords([(5, 5), (3, 3), (7, 7), (5, 2)]),
        hazards=(),
    ),
)


def scenario_names() -> list[str]:
    return [scenario.name for scenario in SCENARIOS]


def get_scenario(name: str) -> Scenario:
    for scenario in SCENARIOS:
        if scenario.name == name:
            return scenario
    raise KeyError(f"unknown scenario: {name}")


def build_board(scenario: Scenario) -> Board:
    return Board(
        width=scenario.width,
        height=scenario.height,
        snakes={snake.id: snake for snake in scenario.snakes},
        food=scenario.food,
        hazards=scenario.hazards,
        ruleset_name=scenario.ruleset_name,
        hazard_damage=scenario.hazard_damage,
    )
