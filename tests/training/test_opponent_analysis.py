from __future__ import annotations

import unittest

import pandas as pd

from battlesnake.training.opponent_model.analysis import dataset_summary


class OpponentAnalysisTests(unittest.TestCase):
    def test_dataset_summary_reports_splits_and_move_distribution(self) -> None:
        frame = pd.DataFrame(
            [
                {"observation_id": "g1:1:s1", "game_id": "g1", "split": "train", "snake_name": "Alpha", "candidate_move": "up", "label": 0},
                {"observation_id": "g1:1:s1", "game_id": "g1", "split": "train", "snake_name": "Alpha", "candidate_move": "right", "label": 1},
                {"observation_id": "g2:1:s2", "game_id": "g2", "split": "test", "snake_name": "Beta", "candidate_move": "down", "label": 1},
            ]
        )
        summary = dataset_summary(frame)
        self.assertEqual(summary["rows"], 3)
        self.assertEqual(summary["observations"], 2)
        self.assertEqual(summary["splits"]["train"], 2)
        self.assertEqual(summary["target_moves"]["right"], 1)
        self.assertEqual(summary["target_moves"]["down"], 1)


if __name__ == "__main__":
    unittest.main()
