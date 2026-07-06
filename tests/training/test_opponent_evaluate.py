from __future__ import annotations

import unittest

import pandas as pd

from battlesnake.training.opponent_model.evaluate import grouped_move_metrics


class OpponentEvaluateTests(unittest.TestCase):
    def test_grouped_move_metrics_use_best_candidate_per_observation(self) -> None:
        frame = pd.DataFrame(
            [
                {"observation_id": "o1", "snake_name": "Alpha", "candidate_move": "up", "label": 0, "score": 0.2},
                {"observation_id": "o1", "snake_name": "Alpha", "candidate_move": "right", "label": 1, "score": 0.8},
                {"observation_id": "o2", "snake_name": "Beta", "candidate_move": "left", "label": 1, "score": 0.4},
                {"observation_id": "o2", "snake_name": "Beta", "candidate_move": "down", "label": 0, "score": 0.6},
            ]
        )
        metrics = grouped_move_metrics(frame, score_column="score")
        self.assertEqual(metrics["observations"], 2)
        self.assertEqual(metrics["top1_correct"], 1)
        self.assertEqual(metrics["top1_accuracy"], 0.5)
        self.assertEqual(metrics["actual_move_mean_score"], 0.6)
        self.assertEqual(metrics["per_snake"]["Alpha"]["top1_accuracy"], 1.0)


if __name__ == "__main__":
    unittest.main()
