"""FastAPI entrypoint for the Battlesnake dev snake.

Production traffic is served by the native C server; this app is the Python
dev snake used to iterate on Standard FFA strategies. Its identity, strategy
variant, and timeout margin are controlled via environment variables so arena
A/B runs can pin exact configurations without code edits:

- ``STRATEGY_VARIANT``: standard FFA strategy variant (default ``first-safe``).
- ``GIT_REVISION``: revision reported in ``/`` version (auto-detected if unset).
- ``SNAKE_COLOR`` / ``SNAKE_HEAD`` / ``SNAKE_TAIL``: appearance overrides.
- ``MOVE_SAFETY_MARGIN_MS``: reserve subtracted from the game timeout before
  the internal decide deadline triggers the first-safe fallback.
"""

from __future__ import annotations

import logging
import json
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from concurrent.futures import TimeoutError as FutureTimeoutError
from pathlib import Path
from typing import Any, Callable

if __package__ in (None, ""):
    current_dir = Path(__file__).resolve().parent
    project_root = current_dir.parent
    sys.path = [path for path in sys.path if Path(path or ".").resolve() != current_dir]
    sys.path.insert(0, str(project_root))

from fastapi import FastAPI

from battlesnake.decision_telemetry import record_decision, record_game_end
from battlesnake.game import Board, board_from_game_state
from battlesnake.strategies.base import Strategy
from battlesnake.strategies.constrictor import StrategyConstrictor
from battlesnake.strategies.duel import StrategyDuel
from battlesnake.strategies.first_safe import StrategyFirstSafe
from battlesnake.strategies.royale import StrategyRoyale
from battlesnake.strategies.standard import StrategyStandard
from battlesnake.types import GameState, Move

logger = logging.getLogger("battlesnake.dev_snake")
app = FastAPI(title="Battlesnake Dev Snake", version="0.1.0")

BASE_VERSION = "0.1.0-dev"
DEFAULT_STRATEGY_VARIANT = "first-safe"
DEFAULT_SNAKE_COLOR = "#f59e0b"
DEFAULT_GAME_TIMEOUT_MS = 500
DEFAULT_MOVE_SAFETY_MARGIN_MS = 150
MIN_MOVE_DEADLINE_MS = 50
DEFAULT_SEARCH_BUDGET_MS = 400

# Standard FFA strategy variants selectable via STRATEGY_VARIANT. New dev
# snake variants register here; duel/royale/constrictor routing is unaffected.
def _standard_v1_strategy() -> Strategy:
    theta_path = Path(__file__).resolve().parent.parent / "configs" / "evaluation_weights" / "standard-ffa-v1-tuned.json"
    return StrategyStandard(theta=json.loads(theta_path.read_text(encoding="utf-8")))


STANDARD_VARIANTS: dict[str, Callable[[], Strategy]] = {
    "first-safe": StrategyFirstSafe,
    "standard-v1": _standard_v1_strategy,
}

_decide_executor = ThreadPoolExecutor(max_workers=8, thread_name_prefix="decide")


def _detect_git_revision() -> str:
    """Return the short git revision from env or the local checkout."""

    env_revision = os.environ.get("GIT_REVISION", "").strip()
    if env_revision:
        return env_revision
    try:
        result = subprocess.run(
            ["git", "rev-parse", "--short", "HEAD"],
            capture_output=True,
            text=True,
            timeout=5,
            check=True,
            cwd=Path(__file__).resolve().parent,
        )
        return result.stdout.strip() or "unknown"
    except (OSError, subprocess.SubprocessError):
        return "unknown"


GIT_REVISION = _detect_git_revision()


def strategy_variant() -> str:
    """Return the configured standard FFA strategy variant name."""

    return os.environ.get("STRATEGY_VARIANT", DEFAULT_STRATEGY_VARIANT).strip() or DEFAULT_STRATEGY_VARIANT


def _env_int(name: str, fallback: int, minimum: int = 1) -> int:
    try:
        value = int(os.environ.get(name, fallback))
    except ValueError:
        return fallback
    return value if value >= minimum else fallback


def effective_search_budget_ms(game_timeout_ms: int | None) -> int:
    """Return the minimax budget clamped to the request deadline."""

    env_budget = _env_int("BATTLESNAKE_SEARCH_BUDGET_MS", DEFAULT_SEARCH_BUDGET_MS)
    return min(env_budget, move_deadline_ms(game_timeout_ms))


def select_strategy(state: GameState) -> Strategy:
    """Select a strategy implementation from the Battlesnake ruleset."""

    ruleset_name = state.game.ruleset.name.lower()
    if ruleset_name == "royale":
        return StrategyRoyale()
    if ruleset_name == "constrictor":
        return StrategyConstrictor()
    if ruleset_name in {"solo", "standard"} and len(state.board.snakes) == 2:
        return StrategyDuel(time_budget_ms=effective_search_budget_ms(state.game.timeout))

    variant = strategy_variant()
    factory = STANDARD_VARIANTS.get(variant)
    if factory is None:
        logger.warning("unknown STRATEGY_VARIANT %r, using %r", variant, DEFAULT_STRATEGY_VARIANT)
        factory = STANDARD_VARIANTS[DEFAULT_STRATEGY_VARIANT]
    return factory()


def move_deadline_ms(game_timeout_ms: int | None) -> int:
    """Return the internal decide deadline for a game timeout."""

    timeout = game_timeout_ms or DEFAULT_GAME_TIMEOUT_MS
    margin = _env_int("MOVE_SAFETY_MARGIN_MS", DEFAULT_MOVE_SAFETY_MARGIN_MS, minimum=0)
    return max(MIN_MOVE_DEADLINE_MS, timeout - margin)


def fallback_move(board: Board, snake_id: str) -> Move | str:
    """Return the first immediate safe move, or up when no safe move exists."""

    moves = board.safe_moves(snake_id)
    return moves[0] if moves else Move.UP


def move_response_value(move: Move | str) -> str:
    """Return the API string for either a Move enum or a native move string."""

    return move.value if isinstance(move, Move) else str(move)


@app.get("/")
def info() -> dict[str, Any]:
    """Return Battlesnake appearance metadata identifying the dev variant."""

    return {
        "apiversion": "1",
        "author": "codex",
        "color": os.environ.get("SNAKE_COLOR", DEFAULT_SNAKE_COLOR),
        "head": os.environ.get("SNAKE_HEAD", "default"),
        "tail": os.environ.get("SNAKE_TAIL", "default"),
        "version": f"{BASE_VERSION}+{strategy_variant()}.{GIT_REVISION}",
    }


@app.post("/start")
def start(state: GameState) -> dict[str, str]:
    """Handle Battlesnake game start."""

    return {"message": f"Starting game {state.game.id}"}


@app.post("/move")
def move(state: GameState) -> dict[str, str]:
    """Select and return a move for the current Battlesnake turn.

    The strategy runs in a worker thread under a hard internal deadline; on
    timeout or any strategy failure the response falls back to the first safe
    move, so the snake never times out at the game level or loses a turn to a
    server error.
    """

    board = board_from_game_state(state)
    strategy = select_strategy(state)
    if hasattr(strategy, "set_context"):
        strategy.set_context(turn=state.turn)
    deadline_ms = move_deadline_ms(state.game.timeout)
    fallback_reason: str | None = None

    future = _decide_executor.submit(strategy.decide, board, state.you.id)
    try:
        selected_move = future.result(timeout=deadline_ms / 1000.0)
    except FutureTimeoutError:
        future.cancel()
        logger.warning(
            "decide exceeded %d ms deadline (game=%s turn=%d), using fallback",
            deadline_ms,
            state.game.id,
            state.turn,
        )
        selected_move = fallback_move(board, state.you.id)
        fallback_reason = "endpoint_deadline"
    except NotImplementedError:
        selected_move = fallback_move(board, state.you.id)
        fallback_reason = "not_implemented"
    except Exception:
        logger.exception("decide failed (game=%s turn=%d), using fallback", state.game.id, state.turn)
        selected_move = fallback_move(board, state.you.id)
        fallback_reason = "strategy_exception"

    selected_value = move_response_value(selected_move)
    try:
        strategy_record = None if fallback_reason == "endpoint_deadline" else getattr(strategy, "last_decision_record", None)
        record = dict(strategy_record) if isinstance(strategy_record, dict) else {}
        record.update(
            {
                "game_id": state.game.id,
                "turn": state.turn,
                "snake_id": state.you.id,
                "variant": strategy_variant(),
                "chosen_move": selected_value,
                "fallback_used": fallback_reason is not None or record.get("fallback_reason") is not None,
                "fallback_reason": record.get("fallback_reason") or fallback_reason,
            }
        )
        record_decision(record)
    except Exception:
        logger.exception("decision telemetry failed (game=%s turn=%d)", state.game.id, state.turn)

    return {"move": selected_value}


@app.post("/end")
def end(state: GameState) -> dict[str, str]:
    """Handle Battlesnake game end."""

    try:
        record_game_end(state)
    except Exception:
        logger.exception("game-end telemetry failed (game=%s turn=%d)", state.game.id, state.turn)
    return {"message": f"Finished game {state.game.id}"}
