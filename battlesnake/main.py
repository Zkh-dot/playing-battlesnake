"""FastAPI entrypoint for the Battlesnake bot."""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Any

if __package__ in (None, ""):
    current_dir = Path(__file__).resolve().parent
    project_root = current_dir.parent
    sys.path = [path for path in sys.path if Path(path or ".").resolve() != current_dir]
    sys.path.insert(0, str(project_root))

from fastapi import FastAPI, HTTPException

from battlesnake.game import Board
from battlesnake.strategies.constrictor import StrategyConstrictor
from battlesnake.strategies.duel import StrategyDuel
from battlesnake.strategies.royale import StrategyRoyale
from battlesnake.strategies.standard import StrategyStandard
from battlesnake.types import GameState, Move

app = FastAPI(title="Battlesnake Bot", version="0.1.0")


def select_strategy(state: GameState) -> StrategyStandard | StrategyRoyale | StrategyConstrictor | StrategyDuel:
    """Select a strategy implementation from the Battlesnake ruleset."""

    ruleset_name = state.game.ruleset.name.lower()
    if ruleset_name == "royale":
        return StrategyRoyale()
    if ruleset_name == "constrictor":
        return StrategyConstrictor()
    if ruleset_name == "solo" and len(state.board.snakes) == 2:
        return StrategyDuel()
    return StrategyStandard()


def fallback_move(board: Board, snake_id: str) -> Move:
    """Return the first immediate safe move, or up when no safe move exists."""

    moves = board.safe_moves(snake_id)
    return moves[0] if moves else Move.UP


@app.get("/")
def info() -> dict[str, Any]:
    """Return Battlesnake appearance metadata."""

    return {
        "apiversion": "1",
        "author": "codex",
        "color": "#2563eb",
        "head": "default",
        "tail": "default",
        "version": "0.1.0",
    }


@app.post("/start")
def start(state: GameState) -> dict[str, str]:
    """Handle Battlesnake game start."""

    return {"message": f"Starting game {state.game.id}"}


@app.post("/move")
def move(state: GameState) -> dict[str, str]:
    """Select and return a move for the current Battlesnake turn."""

    board = Board.from_game_state(state)
    strategy = select_strategy(state)

    try:
        selected_move = strategy.decide(board, state.you.id)
    except NotImplementedError:
        selected_move = fallback_move(board, state.you.id)
    except Exception as exc:
        raise HTTPException(status_code=500, detail=str(exc)) from exc

    return {"move": selected_move.value}


@app.post("/end")
def end(state: GameState) -> dict[str, str]:
    """Handle Battlesnake game end."""

    return {"message": f"Finished game {state.game.id}"}
