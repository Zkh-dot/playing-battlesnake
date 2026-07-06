from __future__ import annotations

import math
import unittest

import pandas as pd

from battlesnake.training.opponent_model.evaluate import grouped_move_metrics
from battlesnake.training.opponent_model.schema import MOVES


def _observation_rows(
    observation_id: str,
    *,
    actual_move: str,
    scores: dict[str, float],
    snake_id: str | None = None,
    snake_name: str = "Alpha",
) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for move in MOVES:
        row: dict[str, object] = {
            "observation_id": observation_id,
            "snake_name": snake_name,
            "candidate_move": move,
            "label": int(move == actual_move),
            "score": scores[move],
        }
        if snake_id is not None:
            row["snake_id"] = snake_id
        rows.append(row)
    return rows


class OpponentEvaluateTests(unittest.TestCase):
    def test_grouped_move_metrics_use_best_candidate_per_observation(self) -> None:
        frame = pd.DataFrame(
            _observation_rows(
                "o1",
                snake_name="Alpha",
                actual_move="right",
                scores={"up": 0.2, "down": 0.1, "left": 0.3, "right": 0.8},
            )
            + _observation_rows(
                "o2",
                snake_name="Beta",
                actual_move="left",
                scores={"up": 0.1, "down": 0.6, "left": 0.4, "right": 0.2},
            )
        )
        metrics = grouped_move_metrics(frame, score_column="score")
        self.assertEqual(metrics["observations"], 2)
        self.assertEqual(metrics["top1_correct"], 1)
        self.assertEqual(metrics["top1_accuracy"], 0.5)
        self.assertAlmostEqual(metrics["actual_move_mean_score"], 0.6)
        self.assertEqual(metrics["per_snake"]["Alpha"]["top1_accuracy"], 1.0)

    def test_empty_frame_returns_zero_metrics(self) -> None:
        metrics = grouped_move_metrics(pd.DataFrame(), score_column="score")
        self.assertEqual(metrics["observations"], 0)
        self.assertEqual(metrics["top1_correct"], 0)
        self.assertEqual(metrics["top1_accuracy"], 0.0)
        self.assertEqual(metrics["per_snake"], {})

    def test_per_snake_metrics_use_snake_id_when_available(self) -> None:
        frame = pd.DataFrame(
            _observation_rows(
                "o1",
                snake_id="s1",
                snake_name="Shared",
                actual_move="up",
                scores={"up": 0.9, "down": 0.1, "left": 0.2, "right": 0.3},
            )
            + _observation_rows(
                "o2",
                snake_id="s2",
                snake_name="Shared",
                actual_move="right",
                scores={"up": 0.7, "down": 0.1, "left": 0.2, "right": 0.3},
            )
        )
        metrics = grouped_move_metrics(frame, score_column="score")
        self.assertEqual(set(metrics["per_snake"]), {"s1", "s2"})
        self.assertEqual(metrics["per_snake"]["s1"]["top1_accuracy"], 1.0)
        self.assertEqual(metrics["per_snake"]["s2"]["top1_accuracy"], 0.0)

    def test_invalid_groups_raise_value_error(self) -> None:
        cases = [
            (
                "missing positive",
                pd.DataFrame(
                    [
                        {"observation_id": "o1", "snake_name": "Alpha", "candidate_move": move, "label": 0, "score": 0.1}
                        for move in MOVES
                    ]
                ),
                "positive labels",
            ),
            (
                "duplicate positives",
                pd.DataFrame(
                    [
                        {
                            "observation_id": "o1",
                            "snake_name": "Alpha",
                            "candidate_move": move,
                            "label": int(move in {"up", "down"}),
                            "score": 0.1,
                        }
                        for move in MOVES
                    ]
                ),
                "positive labels",
            ),
            (
                "candidate count",
                pd.DataFrame(
                    [
                        {"observation_id": "o1", "snake_name": "Alpha", "candidate_move": "up", "label": 1, "score": 0.1},
                        {"observation_id": "o1", "snake_name": "Alpha", "candidate_move": "down", "label": 0, "score": 0.1},
                        {"observation_id": "o1", "snake_name": "Alpha", "candidate_move": "left", "label": 0, "score": 0.1},
                    ]
                ),
                "expected 4",
            ),
            (
                "duplicate candidate moves",
                pd.DataFrame(
                    [
                        {"observation_id": "o1", "snake_name": "Alpha", "candidate_move": "up", "label": 1, "score": 0.1},
                        {"observation_id": "o1", "snake_name": "Alpha", "candidate_move": "up", "label": 0, "score": 0.1},
                        {"observation_id": "o1", "snake_name": "Alpha", "candidate_move": "left", "label": 0, "score": 0.1},
                        {"observation_id": "o1", "snake_name": "Alpha", "candidate_move": "right", "label": 0, "score": 0.1},
                    ]
                ),
                "duplicate candidate moves",
            ),
        ]
        for name, frame, message in cases:
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, message):
                grouped_move_metrics(frame, score_column="score")

    def test_all_zero_or_negative_scores_normalize_uniformly(self) -> None:
        frame = pd.DataFrame(
            _observation_rows(
                "o1",
                actual_move="left",
                scores={"up": 0.0, "down": -1.0, "left": -2.0, "right": 0.0},
            )
        )
        metrics = grouped_move_metrics(frame, score_column="score")
        self.assertEqual(metrics["actual_move_mean_score"], -2.0)
        self.assertAlmostEqual(metrics["actual_move_mean_probability"], 0.25)
        self.assertAlmostEqual(metrics["group_negative_log_loss"], math.log(4.0))

    def test_tie_breaks_use_move_order_not_input_row_order(self) -> None:
        rows = _observation_rows(
            "o1",
            actual_move="down",
            scores={"up": 0.5, "down": 0.5, "left": 0.5, "right": 0.5},
        )
        metrics = grouped_move_metrics(pd.DataFrame(rows), score_column="score")
        reversed_metrics = grouped_move_metrics(pd.DataFrame(list(reversed(rows))), score_column="score")
        self.assertEqual(metrics["top1_correct"], 0)
        self.assertEqual(reversed_metrics["top1_correct"], 0)
        self.assertEqual(metrics["top1_accuracy"], reversed_metrics["top1_accuracy"])


if __name__ == "__main__":
    unittest.main()
