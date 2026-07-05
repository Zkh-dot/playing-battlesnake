from __future__ import annotations

import json
from typing import Any

from benchmarks.scenarios import SCENARIOS, Scenario


def coord_json(coord: Any) -> dict[str, int]:
    return {"x": int(coord.x), "y": int(coord.y)}


def snake_json(snake: Any) -> dict[str, Any]:
    body = [coord_json(coord) for coord in snake.body]
    return {
        "id": snake.id,
        "name": snake.name,
        "health": snake.health,
        "body": body,
        "latency": "0",
        "head": body[0],
        "length": snake.length,
        "shout": "",
        "customizations": {"color": "#2563eb", "head": "default", "tail": "default"},
    }


def move_payload(scenario: Scenario, turn: int = 14, timeout: int = 500) -> str:
    snakes = [snake_json(snake) for snake in scenario.snakes]
    you = next(snake for snake in snakes if snake["id"] == scenario.snake_id)
    payload = {
        "game": {
            "id": f"bench-{scenario.name}",
            "ruleset": {
                "name": scenario.ruleset_name,
                "version": "v1",
                "settings": {
                    "foodSpawnChance": 15,
                    "minimumFood": 1,
                    "hazardDamagePerTurn": scenario.hazard_damage,
                    "royale": {"shrinkEveryNTurns": 5},
                    "squad": {
                        "allowBodyCollisions": False,
                        "sharedElimination": False,
                        "sharedHealth": False,
                        "sharedLength": False,
                    },
                },
            },
            "map": "standard",
            "source": "custom",
            "timeout": timeout,
        },
        "turn": turn,
        "board": {
            "height": scenario.height,
            "width": scenario.width,
            "food": [coord_json(coord) for coord in scenario.food],
            "hazards": [coord_json(coord) for coord in scenario.hazards],
            "snakes": snakes,
        },
        "you": you,
    }
    return json.dumps(payload, separators=(",", ":"), sort_keys=True)


def payload_by_name(name: str) -> str:
    for scenario in SCENARIOS:
        if scenario.name == name:
            return move_payload(scenario)
    raise KeyError(f"unknown scenario: {name}")
