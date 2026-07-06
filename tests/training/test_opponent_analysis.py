from __future__ import annotations

import unittest

import pandas as pd

from battlesnake.training.opponent_model.analysis import dataset_summary


class OpponentAnalysisTests(unittest.TestCase):
    def test_dataset_summary_reports_splits_and_move_distribution(self) -> None:
        frame = pd.DataFrame(
            [
                {"observation_id": "g1:1:s1", "game_id": "g1", "split": "train", "snake_id": "s1", "snake_name": "Alpha", "candidate_move": "up", "label": 0},
                {"observation_id": "g1:1:s1", "game_id": "g1", "split": "train", "snake_id": "s1", "snake_name": "Alpha", "candidate_move": "right", "label": 1},
                {"observation_id": "g2:1:s2", "game_id": "g2", "split": "test", "snake_id": "s2", "snake_name": "Beta", "candidate_move": "down", "label": 1},
            ]
        )
        summary = dataset_summary(frame)
        self.assertEqual(summary["rows"], 3)
        self.assertEqual(summary["observations"], 2)
        self.assertEqual(summary["splits"]["train"], 2)
        self.assertEqual(summary["target_moves"]["right"], 1)
        self.assertEqual(summary["target_moves"]["down"], 1)

    def test_dataset_summary_counts_snake_ids_not_display_names(self) -> None:
        frame = pd.DataFrame(
            [
                {"observation_id": "g1:1:s1", "game_id": "g1", "split": "train", "snake_id": "s1", "snake_name": "Alpha", "candidate_move": "up", "label": 1},
                {"observation_id": "g1:1:s2", "game_id": "g1", "split": "train", "snake_id": "s2", "snake_name": "Alpha", "candidate_move": "left", "label": 1},
            ]
        )
        summary = dataset_summary(frame)
        self.assertEqual(summary["snakes"], 2)
        self.assertEqual(summary["top_snakes_by_observations"], {"s1": 1, "s2": 1})

    def test_dataset_summary_handles_empty_frame(self) -> None:
        frame = pd.DataFrame(
            columns=[
                "observation_id",
                "game_id",
                "split",
                "snake_id",
                "snake_name",
                "candidate_move",
                "label",
            ]
        )
        summary = dataset_summary(frame)
        self.assertEqual(summary["rows"], 0)
        self.assertEqual(summary["observations"], 0)
        self.assertEqual(summary["games"], 0)
        self.assertEqual(summary["snakes"], 0)
        self.assertEqual(summary["splits"], {})
        self.assertEqual(summary["target_moves"], {})
        self.assertEqual(summary["top_snakes_by_observations"], {})


if __name__ == "__main__":
    unittest.main()
