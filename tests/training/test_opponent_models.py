from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

import numpy as np
import pandas as pd

from battlesnake.training.opponent_model.models import (
    FEATURE_COLUMNS,
    build_gbdt_pipeline,
    build_logistic_pipeline,
    build_move_prior_model,
    fit_model,
    save_model,
    score_frame,
)


def tiny_frame() -> pd.DataFrame:
    rows = []
    for obs, actual in [("o1", "right"), ("o2", "left"), ("o3", "up"), ("o4", "down")]:
        for move in ["up", "down", "left", "right"]:
            rows.append(
                {
                    "observation_id": obs,
                    "game_id": obs,
                    "split": "train",
                    "snake_id": "s1",
                    "snake_name": "Alpha",
                    "candidate_move": move,
                    "label": 1 if move == actual else 0,
                    "feature_candidate_move": move,
                    "feature_turn": 1.0,
                    "board_width": 11.0,
                    "board_height": 11.0,
                    "alive_snakes": 4.0,
                    "feature_snake_rank": 1.0,
                    "snake_health": 90.0,
                    "snake_length": 3.0,
                    "safe_moves_count": 3.0,
                    "candidate_is_safe": 1.0,
                    "candidate_in_bounds": 1.0,
                    "candidate_occupied_without_tails": 0.0,
                    "candidate_is_food": 0.0,
                    "candidate_is_hazard": 0.0,
                    "candidate_to_nearest_food": 3.0,
                    "head_to_nearest_food": 4.0,
                    "candidate_center_distance": 2.0,
                    "candidate_reachable_space": 40.0,
                    "adjacent_longer_or_equal_heads": 0.0,
                    "adjacent_shorter_heads": 0.0,
                }
            )
    return pd.DataFrame(rows)


class OpponentModelTests(unittest.TestCase):
    def test_feature_columns_are_present_in_candidate_frame(self) -> None:
        frame = tiny_frame()
        self.assertEqual(set(FEATURE_COLUMNS) - set(frame.columns), set())

    def test_models_fit_and_predict_positive_class_probabilities(self) -> None:
        frame = tiny_frame()
        models = [
            build_move_prior_model(),
            build_logistic_pipeline(),
            build_gbdt_pipeline(max_iter=5),
        ]
        for model in models:
            with self.subTest(model=type(model).__name__):
                fitted = fit_model(model, frame)
                probabilities = fitted.predict_proba(frame[FEATURE_COLUMNS])[:, 1]
                self.assertEqual(len(probabilities), len(frame))

    def test_save_model_writes_joblib_file(self) -> None:
        frame = tiny_frame()
        model = fit_model(build_move_prior_model(), frame)
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "nested" / "model.joblib"
            save_model(model, path)
            self.assertTrue(path.exists())
            self.assertGreater(path.stat().st_size, 0)

    def test_score_frame_adds_positive_class_score(self) -> None:
        frame = tiny_frame()
        model = fit_model(build_move_prior_model(), frame)
        scored = score_frame(model, frame)

        self.assertEqual(len(scored), len(frame))
        self.assertIn("score", scored.columns)
        self.assertTrue(scored["score"].between(0.0, 1.0).all())

    def test_move_prior_fit_uses_positional_labels_with_non_default_index(self) -> None:
        frame = tiny_frame().iloc[[0, 1, 2, 3]].copy()
        frame.index = [10, 11, 12, 13]
        labels = pd.Series([1, 0, 0, 0])

        model = build_move_prior_model().fit(frame[FEATURE_COLUMNS], labels)

        self.assertEqual(model.move_probabilities["up"], 1.0)
        self.assertEqual(model.move_probabilities["down"], 0.0)

    def test_score_frame_uses_classes_to_find_positive_probability(self) -> None:
        class ReversedClassModel:
            classes_ = np.array([1, 0])

            def predict_proba(self, features: pd.DataFrame) -> np.ndarray:
                return np.column_stack(
                    [
                        np.full(len(features), 0.7),
                        np.full(len(features), 0.3),
                    ]
                )

        scored = score_frame(ReversedClassModel(), tiny_frame())

        self.assertTrue((scored["score"] == 0.7).all())


if __name__ == "__main__":
    unittest.main()
