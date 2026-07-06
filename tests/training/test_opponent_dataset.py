from __future__ import annotations

import csv
import tempfile
import unittest
from pathlib import Path

from battlesnake.training.opponent_model.dataset import BASE_COLUMNS, write_candidate_rows, write_candidate_rows_streaming
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
            with out.open() as handle:
                written = list(csv.DictReader(handle))

        self.assertEqual(summary["rows"], 2)
        self.assertEqual(summary["observations"], 1)
        self.assertEqual(written[1]["candidate_move"], "right")
        self.assertEqual(written[1]["feature_candidate_move"], "right")
        self.assertEqual(written[1]["label"], "1")
        self.assertEqual(written[1]["snake_health"], "90.0")

    def test_write_candidate_rows_namespaces_colliding_feature_columns(self) -> None:
        rows = [
            CandidateRow(
                observation_id="g1:1:s1",
                game_id="g1",
                split="train",
                turn=1,
                snake_id="s1",
                snake_name="Alpha",
                snake_rank=None,
                candidate_move="up",
                label=1,
                features={
                    "candidate_move": "up",
                    "snake_health": 90.0,
                    "snake_rank": 999.0,
                    "turn": 123.0,
                },
            )
        ]
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "rows.csv"
            write_candidate_rows(rows, out)
            with out.open() as handle:
                reader = csv.DictReader(handle)
                written = list(reader)
                header = reader.fieldnames

        self.assertEqual(
            header,
            BASE_COLUMNS
            + ["feature_candidate_move", "feature_snake_rank", "feature_turn", "snake_health"],
        )
        self.assertEqual(written[0]["snake_rank"], "")
        self.assertEqual(written[0]["feature_snake_rank"], "999.0")
        self.assertEqual(written[0]["feature_candidate_move"], "up")
        self.assertEqual(written[0]["feature_turn"], "123.0")
        self.assertEqual(written[0]["snake_health"], "90.0")

    def test_write_candidate_rows_writes_base_only_header_for_empty_input(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "rows.csv"
            summary = write_candidate_rows([], out)
            with out.open() as handle:
                reader = csv.DictReader(handle)
                written = list(reader)
                header = reader.fieldnames

        self.assertEqual(header, BASE_COLUMNS)
        self.assertEqual(written, [])
        self.assertEqual(summary["rows"], 0)
        self.assertEqual(summary["observations"], 0)
        self.assertEqual(summary["games"], 0)
        self.assertEqual(summary["snakes"], 0)
        self.assertEqual(summary["split_rows"], {})

    def test_write_candidate_rows_counts_snake_ids_not_names(self) -> None:
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
                label=1,
                features={},
            ),
            CandidateRow(
                observation_id="g1:1:s2",
                game_id="g1",
                split="train",
                turn=1,
                snake_id="s2",
                snake_name="Alpha",
                snake_rank=2,
                candidate_move="left",
                label=1,
                features={},
            ),
        ]
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "rows.csv"
            summary = write_candidate_rows(rows, out)

        self.assertEqual(summary["snakes"], 2)

    def test_write_candidate_rows_streaming_writes_without_materializing(self) -> None:
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
            summary = write_candidate_rows_streaming(iter(rows), out)
            with out.open() as handle:
                reader = csv.DictReader(handle)
                written = list(reader)
                header = reader.fieldnames

        self.assertEqual(summary["rows"], 2)
        self.assertEqual(summary["observations"], 1)
        self.assertEqual(summary["games"], 1)
        self.assertEqual(summary["snakes"], 1)
        self.assertEqual(summary["split_rows"], {"train": 2})
        self.assertEqual(header, BASE_COLUMNS + ["feature_candidate_move", "snake_health"])
        self.assertEqual(written[1]["feature_candidate_move"], "right")


if __name__ == "__main__":
    unittest.main()
