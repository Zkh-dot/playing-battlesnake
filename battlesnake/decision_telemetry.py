"""JSONL decision telemetry for the Python dev snake."""

from __future__ import annotations

import json
import os
from concurrent.futures import Future, ThreadPoolExecutor
from pathlib import Path
from typing import Any

from battlesnake.types import GameState


DEFAULT_DECISION_LOG = Path("logs/standard-decisions.jsonl")
_DISABLED_VALUES = {"0", "false", "no", "off"}
_writer = ThreadPoolExecutor(max_workers=1, thread_name_prefix="decision-telemetry")
_pending: list[Future[None]] = []
_last_decisions: dict[str, dict[str, Any]] = {}


def telemetry_enabled() -> bool:
    value = os.environ.get("STANDARD_DECISION_TELEMETRY", "1").strip().lower()
    return value not in _DISABLED_VALUES


def decision_log_path() -> Path:
    return Path(os.environ.get("STANDARD_DECISION_LOG", str(DEFAULT_DECISION_LOG)))


def record_decision(record: dict[str, Any]) -> None:
    """Queue one per-turn decision record for JSONL output."""

    if not telemetry_enabled():
        return
    enriched = {"type": "decision", **record}
    game_id = enriched.get("game_id")
    if isinstance(game_id, str):
        _last_decisions[game_id] = enriched
    _enqueue(enriched)


def record_game_end(state: GameState) -> dict[str, Any] | None:
    """Queue a game-end telemetry record and return it for tests."""

    if not telemetry_enabled():
        return None
    last_decision = _last_decisions.pop(state.game.id, None)
    record = {
        "type": "game_end",
        "game_id": state.game.id,
        "turn": state.turn,
        "snake_id": state.you.id,
        "death_cause": classify_death_cause(state, last_decision),
    }
    _enqueue(record)
    return record


def classify_death_cause(state: GameState, last_decision: dict[str, Any] | None = None) -> str:
    alive_ids = {snake.id for snake in state.board.snakes}
    if state.you.id in alive_ids:
        return "won" if len(alive_ids) <= 1 else "alive"

    head = state.you.head
    if head is not None and (
        head.x < 0 or head.x >= state.board.width or head.y < 0 or head.y >= state.board.height
    ):
        return "wall"
    if state.you.health <= 0:
        return "starvation"

    chosen = _chosen_candidate(last_decision)
    if chosen is None:
        return "unknown"
    death_class = chosen.get("death_class")
    if death_class == "head_to_head_losing":
        return "head_to_head"
    if death_class == "hazard_starvation":
        return "starvation"
    if death_class in {"wall", "body", "self"}:
        return str(death_class)
    return "unknown"


def flush(timeout: float | None = 5.0) -> None:
    """Wait for queued telemetry writes. Intended for tests and shutdown hooks."""

    while _pending:
        future = _pending.pop(0)
        future.result(timeout=timeout)


def _chosen_candidate(last_decision: dict[str, Any] | None) -> dict[str, Any] | None:
    if not last_decision:
        return None
    chosen_move = last_decision.get("chosen_move")
    for candidate in last_decision.get("candidates", []):
        if isinstance(candidate, dict) and candidate.get("move") == chosen_move:
            return candidate
    return None


def _enqueue(record: dict[str, Any]) -> None:
    _pending[:] = [future for future in _pending if not future.done()]
    future = _writer.submit(_append_jsonl, decision_log_path(), record)
    _pending.append(future)


def _append_jsonl(path: Path, record: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a", encoding="utf-8") as handle:
        json.dump(record, handle, sort_keys=True)
        handle.write("\n")
