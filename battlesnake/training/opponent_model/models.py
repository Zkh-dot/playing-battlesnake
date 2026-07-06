from __future__ import annotations

from pathlib import Path
from typing import Any

import joblib
import numpy as np
import pandas as pd
from sklearn.base import BaseEstimator, ClassifierMixin
from sklearn.compose import ColumnTransformer
from sklearn.ensemble import HistGradientBoostingClassifier
from sklearn.linear_model import LogisticRegression
from sklearn.pipeline import Pipeline
from sklearn.preprocessing import OneHotEncoder, StandardScaler

CATEGORICAL_FEATURES = ["feature_candidate_move", "snake_name"]
NUMERIC_FEATURES = [
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
FEATURE_COLUMNS = CATEGORICAL_FEATURES + NUMERIC_FEATURES


class MovePriorModel(BaseEstimator, ClassifierMixin):
    def __init__(self) -> None:
        self.default_probability = 0.25
        self.move_probabilities: dict[str, float] = {}
        self.classes_ = np.array([0, 1])

    def fit(self, X: pd.DataFrame, y: Any) -> "MovePriorModel":
        moves = _move_column(X).reset_index(drop=True)
        labels = pd.Series(y).reset_index(drop=True).astype(int)
        rates = labels.groupby(moves).mean()
        self.default_probability = float(labels.mean()) if len(labels) else 0.25
        self.move_probabilities = {str(move): float(probability) for move, probability in rates.items()}
        return self

    def predict_proba(self, X: pd.DataFrame) -> np.ndarray:
        moves = _move_column(X)
        positive = moves.astype(str).map(self.move_probabilities).fillna(self.default_probability).to_numpy(dtype=float)
        positive = np.clip(positive, 0.0, 1.0)
        return np.column_stack([1.0 - positive, positive])


def _move_column(frame: pd.DataFrame) -> pd.Series:
    if "feature_candidate_move" in frame.columns:
        return frame["feature_candidate_move"]
    if "candidate_move" in frame.columns:
        return frame["candidate_move"]
    raise KeyError("Expected feature_candidate_move or candidate_move column")


def build_move_prior_model() -> MovePriorModel:
    return MovePriorModel()


def _categorical_encoder(*, sparse_output: bool) -> OneHotEncoder:
    return OneHotEncoder(handle_unknown="ignore", sparse_output=sparse_output)


def build_logistic_pipeline() -> Pipeline:
    preprocessor = ColumnTransformer(
        transformers=[
            ("categorical", _categorical_encoder(sparse_output=True), CATEGORICAL_FEATURES),
            ("numeric", StandardScaler(), NUMERIC_FEATURES),
        ]
    )
    return Pipeline(
        steps=[
            ("preprocess", preprocessor),
            (
                "classifier",
                LogisticRegression(max_iter=1000, class_weight="balanced", random_state=17),
            ),
        ]
    )


def build_gbdt_pipeline(max_iter: int = 220) -> Pipeline:
    preprocessor = ColumnTransformer(
        transformers=[
            ("categorical", _categorical_encoder(sparse_output=False), CATEGORICAL_FEATURES),
            ("numeric", "passthrough", NUMERIC_FEATURES),
        ],
        sparse_threshold=0.0,
    )
    return Pipeline(
        steps=[
            ("preprocess", preprocessor),
            (
                "classifier",
                HistGradientBoostingClassifier(
                    loss="log_loss",
                    learning_rate=0.06,
                    max_iter=max_iter,
                    max_leaf_nodes=31,
                    l2_regularization=0.05,
                    random_state=17,
                ),
            ),
        ]
    )


def fit_model(model: Any, frame: pd.DataFrame) -> Any:
    return model.fit(frame[FEATURE_COLUMNS], frame["label"].astype(int))


def score_frame(model: Any, frame: pd.DataFrame, score_column: str = "score") -> pd.DataFrame:
    scored = frame.copy()
    classes = getattr(model, "classes_", None)
    if classes is None:
        raise ValueError("Model must expose classes_ for positive-class scoring")
    class_matches = np.flatnonzero(np.asarray(classes) == 1)
    if len(class_matches) != 1:
        raise ValueError("Model classes_ must contain exactly one positive class 1")
    scored[score_column] = model.predict_proba(scored[FEATURE_COLUMNS])[:, int(class_matches[0])]
    return scored


def save_model(model: Any, path: Path | str) -> None:
    output_path = Path(path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    joblib.dump(model, output_path)
