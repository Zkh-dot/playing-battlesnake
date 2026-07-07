from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

from battlesnake.battlesnake_native import Board, Coord, Snake
from battlesnake.decision_telemetry import flush, record_decision
from battlesnake.main import end, move
from battlesnake.strategies.standard import StrategyStandard
from battlesnake.types import GameState


def c(x: int, y: int) -> Coord:
    return Coord(x, y)


def snake(snake_id: str, body: list[tuple[int, int]], *, health: int = 100) -> Snake:
    return Snake(id=snake_id, name=snake_id, health=health, body=[c(x, y) for x, y in body])


def board() -> Board:
    return Board(
        7,
        7,
        [
            snake("me", [(2, 2), (2, 1), (2, 0)]),
            snake("north", [(6, 6)]),
            snake("east", [(6, 0)]),
        ],
        food=[c(2, 3)],
    )


def payload(game_id: str = "game-telemetry", turn: int = 3) -> dict:
    snakes = [
        {
            "id": "me",
            "name": "me",
            "health": 100,
            "body": [{"x": 2, "y": 2}, {"x": 2, "y": 1}, {"x": 2, "y": 0}],
        },
        {"id": "north", "name": "north", "health": 100, "body": [{"x": 6, "y": 6}]},
        {"id": "east", "name": "east", "health": 100, "body": [{"x": 6, "y": 0}]},
    ]
    return {
        "game": {"id": game_id, "ruleset": {"name": "standard", "settings": {}}, "timeout": 500},
        "turn": turn,
        "board": {"height": 7, "width": 7, "food": [{"x": 2, "y": 3}], "hazards": [], "snakes": snakes},
        "you": snakes[0],
    }


def test_standard_strategy_exposes_serializable_decision_record() -> None:
    selected, record = StrategyStandard().explain_decision(board(), "me")

    assert selected in {"up", "down", "left", "right"}
    assert record["chosen_move"] == selected
    assert record["candidates"]
    chosen = next(candidate for candidate in record["candidates"] if candidate["move"] == selected)
    assert chosen["scenario_count"] > 0
    assert {"native_evaluate", "space", "head_pressure", "food", "pocket"} <= set(chosen["terms"])
    json.dumps(record)


def test_move_endpoint_writes_decision_jsonl(tmp_path: Path, monkeypatch) -> None:
    log_path = tmp_path / "decisions.jsonl"
    monkeypatch.setenv("STANDARD_DECISION_LOG", str(log_path))
    monkeypatch.setenv("STANDARD_DECISION_TELEMETRY", "1")
    monkeypatch.setenv("STRATEGY_VARIANT", "standard-v1")

    response = move(GameState.model_validate(payload()))
    flush()

    assert response["move"] in {"up", "down", "left", "right"}
    rows = [json.loads(line) for line in log_path.read_text().splitlines()]
    assert rows[0]["type"] == "decision"
    assert rows[0]["game_id"] == "game-telemetry"
    assert rows[0]["candidates"]
    assert rows[0]["fallback_used"] is False


def test_telemetry_can_be_disabled(tmp_path: Path, monkeypatch) -> None:
    log_path = tmp_path / "disabled.jsonl"
    monkeypatch.setenv("STANDARD_DECISION_LOG", str(log_path))
    monkeypatch.setenv("STANDARD_DECISION_TELEMETRY", "0")

    record_decision({"game_id": "disabled", "turn": 1, "chosen_move": "up"})
    flush()

    assert not log_path.exists()


def test_end_endpoint_writes_game_end_death_cause(tmp_path: Path, monkeypatch) -> None:
    log_path = tmp_path / "end.jsonl"
    monkeypatch.setenv("STANDARD_DECISION_LOG", str(log_path))
    monkeypatch.setenv("STANDARD_DECISION_TELEMETRY", "1")

    response = end(GameState.model_validate(payload(game_id="won-game", turn=99)))
    flush()

    assert response["message"] == "Finished game won-game"
    rows = [json.loads(line) for line in log_path.read_text().splitlines()]
    assert rows[0]["type"] == "game_end"
    assert rows[0]["death_cause"] == "alive"


def test_analysis_tool_summarizes_and_prints_turn_table(tmp_path: Path) -> None:
    log_path = tmp_path / "sample.jsonl"
    decision = {
        "type": "decision",
        "game_id": "game-a",
        "turn": 7,
        "chosen_move": "up",
        "fallback_used": False,
        "latency_ms": 1.25,
        "opponent_priors": {},
        "candidates": [
            {
                "move": "up",
                "eligible": True,
                "death_class": None,
                "immediate_safe_move_count_after": 2,
                "immediate_reachable_space": 20,
                "score": 10.0,
                "expected": 8.0,
                "worst": 5.0,
                "scenario_count": 3,
                "target": {"x": 2, "y": 3},
                "terms": {"space": 4.0, "native_evaluate": 6.0},
            }
        ],
    }
    game_end = {"type": "game_end", "game_id": "game-a", "turn": 10, "death_cause": "won"}
    log_path.write_text(json.dumps(decision) + "\n" + json.dumps(game_end) + "\n")

    summary = subprocess.check_output(
        [sys.executable, "tools/analyze_standard_decisions.py", str(log_path)],
        text=True,
    )
    table = subprocess.check_output(
        [
            sys.executable,
            "tools/analyze_standard_decisions.py",
            str(log_path),
            "--game-id",
            "game-a",
            "--turn",
            "7",
        ],
        text=True,
    )

    assert "fallback_rate" in summary
    assert "game-a 1 0.000 2.00 20.00" in summary
    assert "game_end game-a 10 won" in summary
    assert "chosen=up" in table
    assert "space=4.00" in table
