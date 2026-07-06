from __future__ import annotations

import csv
import tempfile
import unittest
from pathlib import Path

from battlesnake.training.opponent_model.dataset import write_candidate_rows
from battlesnake.training.opponent_model.schema import CandidateRow


class OpponentDatasetTests(unittest.TestCase):
    def test_write_candidate_rows_flattens_features_to_csv(self) -> None:
        rows = [
            CandidateRow(
                observation_id="g1:1:s1",
                game_id="g1",
                split="train",
                turn=1,
                snake_id="s1",
                snake_name="Alpha",
                snake_rank=1,
                candidate_move="up",
                label=0,
                features={"candidate_move": "up", "snake_health": 90.0},
            ),
            CandidateRow(
                observation_id="g1:1:s1",
                game_id="g1",
                split="train",
                turn=1,
                snake_id="s1",
                snake_name="Alpha",
                snake_rank=1,
                candidate_move="right",
                label=1,
                features={"candidate_move": "right", "snake_health": 90.0},
            ),
        ]
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "rows.csv"
            summary = write_candidate_rows(rows, out)
            written = list(csv.DictReader(out.open()))

        self.assertEqual(summary["rows"], 2)
        self.assertEqual(summary["observations"], 1)
        self.assertEqual(written[1]["candidate_move"], "right")
        self.assertEqual(written[1]["label"], "1")
        self.assertEqual(written[1]["snake_health"], "90.0")


if __name__ == "__main__":
    unittest.main()
