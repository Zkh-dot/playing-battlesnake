"""Opponent move priors for Standard FFA strategy."""

from __future__ import annotations

import os
import time
from functools import lru_cache
from pathlib import Path
from typing import Any

from battlesnake.battlesnake_native import Board
from battlesnake.opponent_model_features import MOVES, candidate_feature_rows
from battlesnake.strategies.standard_gates import MOVE_ORDER

DEFAULT_MODEL_PATH = Path("ai-artifacts/opponent-model/gbdt_lightgbm.joblib.gz")
DEFAULT_EPSILON = 0.12
DEFAULT_TIMEOUT_MS = 20.0

MODEL_FEATURE_COLUMNS = [
    "feature_candidate_move",
    "feature_turn",
    "board_width",
    "board_height",
    "alive_snakes",
    "feature_snake_rank",
    "snake_health",
    "snake_length",
    "safe_moves_count",
    "candidate_is_safe",
    "candidate_in_bounds",
    "candidate_occupied_without_tails",
    "candidate_is_food",
    "candidate_is_hazard",
    "candidate_to_nearest_food",
    "head_to_nearest_food",
    "candidate_center_distance",
    "candidate_reachable_space",
    "adjacent_longer_or_equal_heads",
    "adjacent_shorter_heads",
]

_BASE_COLUMNS = {
    "observation_id",
    "game_id",
    "split",
    "turn",
    "snake_id",
    "snake_name",
    "snake_rank",
    "candidate_move",
    "label",
}


def uniform_safe_priors(board: Board, snake_id: str) -> dict[str, list[tuple[str, float]]]:
    priors: dict[str, list[tuple[str, float]]] = {}
    for opponent_id in sorted(board.snakes):
        if opponent_id == snake_id:
            continue
        safe_moves = [move for move in MOVE_ORDER if move in set(board.safe_moves(opponent_id))]
        moves = safe_moves or list(MOVE_ORDER)
        probability = 1.0 / len(moves)
        priors[opponent_id] = [(move, probability) for move in moves]
    return priors


class LightGBMOpponentPrior:
    """Runtime wrapper around the trained opponent move model."""

    def __init__(
        self,
        *,
        model_path: Path | str | None = None,
        epsilon: float | None = None,
        timeout_ms: float | None = None,
        model: Any | None = None,
    ) -> None:
        self.model_path = Path(model_path or os.environ.get("STANDARD_OPPONENT_MODEL_PATH", DEFAULT_MODEL_PATH))
        self.epsilon = _env_float("STANDARD_OPPONENT_PRIOR_EPS", DEFAULT_EPSILON) if epsilon is None else epsilon
        self.timeout_ms = (
            _env_float("STANDARD_OPPONENT_PRIOR_TIMEOUT_MS", DEFAULT_TIMEOUT_MS)
            if timeout_ms is None
            else timeout_ms
        )
        self._model = model
        self.last_status = "not_loaded"
        self.last_latency_ms = 0.0

    def priors(self, board: Board, snake_id: str, *, turn: int = 0) -> dict[str, list[tuple[str, float]]]:
        started = time.perf_counter()
        fallback = uniform_safe_priors(board, snake_id)
        try:
            model = self._model if self._model is not None else _load_model(self.model_path)
            priors = self._model_priors(model, board, snake_id, turn=turn, fallback=fallback)
            elapsed_ms = (time.perf_counter() - started) * 1000.0
            self.last_latency_ms = elapsed_ms
            if elapsed_ms > self.timeout_ms:
                self.last_status = "timeout_fallback"
                return fallback
            self.last_status = "model"
            return priors
        except Exception:
            self.last_latency_ms = (time.perf_counter() - started) * 1000.0
            self.last_status = "error_fallback"
            return fallback

    def preload(self) -> bool:
        try:
            model = _load_model(self.model_path)
            _positive_probabilities(model, _warmup_rows())
        except Exception:
            self.last_status = "preload_error"
            return False
        self.last_status = "preloaded"
        return True

    def _model_priors(
        self,
        model: Any,
        board: Board,
        snake_id: str,
        *,
        turn: int,
        fallback: dict[str, list[tuple[str, float]]],
    ) -> dict[str, list[tuple[str, float]]]:
        priors: dict[str, list[tuple[str, float]]] = {}
        for opponent_id, uniform_moves in fallback.items():
            rows = _model_rows(board, opponent_id, turn=turn)
            positive = _positive_probabilities(model, rows)
            safe_moves = [move for move, _probability in uniform_moves]
            model_scores = {move: positive.get(move, 0.0) for move in safe_moves}
            total = sum(model_scores.values())
            if total <= 0.0:
                priors[opponent_id] = uniform_moves
                continue
            uniform_probability = 1.0 / len(safe_moves)
            smoothed = [
                (move, (1.0 - self.epsilon) * (model_scores[move] / total) + self.epsilon * uniform_probability)
                for move in safe_moves
            ]
            priors[opponent_id] = sorted(smoothed, key=lambda item: (-item[1], MOVE_ORDER.index(item[0])))
        return priors


@lru_cache(maxsize=4)
def _load_model(path: Path) -> Any:
    if not path.exists():
        raise FileNotFoundError(path)
    import joblib

    return joblib.load(path)


def _model_rows(board: Board, snake_id: str, *, turn: int) -> list[dict[str, Any]]:
    rows = []
    for row in candidate_feature_rows(
        board=board,
        snake_id=snake_id,
        turn=turn,
        snake_rank=None,
        alive_snakes=len(board.snakes),
    ):
        rows.append({_feature_column_name(key): value for key, value in row.features.items()})
    return rows


def _positive_probabilities(model: Any, rows: list[dict[str, Any]]) -> dict[str, float]:
    import numpy as np
    import pandas as pd

    frame = pd.DataFrame(rows, columns=MODEL_FEATURE_COLUMNS)
    move_by_code = dict(enumerate(MOVES))
    move_codes = {move: code for code, move in move_by_code.items()}
    moves = frame["feature_candidate_move"].astype(str).tolist()
    frame["feature_candidate_move"] = pd.Categorical(
        [move_codes[move] for move in moves],
        categories=list(move_by_code),
    )
    classes = getattr(model, "classes_", None)
    if classes is None:
        raise ValueError("opponent model does not expose classes_")
    class_matches = np.flatnonzero(np.asarray(classes) == 1)
    if len(class_matches) != 1:
        raise ValueError("opponent model classes_ must contain exactly one positive class 1")
    probabilities = model.predict_proba(frame[MODEL_FEATURE_COLUMNS])[:, int(class_matches[0])]
    return {
        str(move): float(probability)
        for move, probability in zip(moves, probabilities)
    }


def _warmup_rows() -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for move in MOVES:
        row = {column: 0.0 for column in MODEL_FEATURE_COLUMNS}
        row.update(
            {
                "feature_candidate_move": move,
                "board_width": 7.0,
                "board_height": 7.0,
                "alive_snakes": 3.0,
                "snake_health": 100.0,
                "snake_length": 3.0,
                "safe_moves_count": 4.0,
                "candidate_is_safe": 1.0,
                "candidate_in_bounds": 1.0,
                "candidate_center_distance": 3.0,
                "candidate_reachable_space": 20.0,
            }
        )
        rows.append(row)
    return rows


def _feature_column_name(name: str) -> str:
    if name in _BASE_COLUMNS:
        return f"feature_{name}"
    return name


def _env_float(name: str, fallback: float) -> float:
    try:
        return float(os.environ.get(name, fallback))
    except ValueError:
        return fallback
