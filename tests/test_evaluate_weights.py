from __future__ import annotations

import json
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_WEIGHTS_PATH = REPO_ROOT / "configs" / "evaluation_weights" / "default.json"
TUNED_WEIGHTS_PATH = REPO_ROOT / "configs" / "evaluation_weights" / "tuned-opponent-pressure.json"


EXPECTED_KEYS = {
    "terminal_win",
    "terminal_loss",
    "base",
    "health",
    "length",
    "reachable_space",
    "safe_moves",
    "center",
    "food",
    "low_health_food",
    "low_health_threshold",
    "hazard_damage",
    "hazard",
    "length_advantage",
    "adjacent_equal_or_longer_penalty",
    "adjacent_shorter_bonus",
    "opponent_reachable_space",
    "territory_delta",
    "opponent_safe_moves",
    "opponent_low_health_food_denial",
}


class WeightConfigTests(unittest.TestCase):
    def test_default_weights_file_contains_all_native_keys(self) -> None:
        weights = json.loads(DEFAULT_WEIGHTS_PATH.read_text())

        self.assertEqual(set(weights), EXPECTED_KEYS)
        self.assertEqual(weights["terminal_win"], 1000000.0)
        self.assertEqual(weights["terminal_loss"], -1000000.0)
        self.assertEqual(weights["opponent_reachable_space"], 0.0)
        self.assertEqual(weights["territory_delta"], 0.0)
        self.assertEqual(weights["opponent_safe_moves"], 0.0)
        self.assertEqual(weights["opponent_low_health_food_denial"], 0.0)

    def test_tuned_weights_file_starts_with_same_keys(self) -> None:
        default_weights = json.loads(DEFAULT_WEIGHTS_PATH.read_text())
        tuned_weights = json.loads(TUNED_WEIGHTS_PATH.read_text())

        self.assertEqual(set(tuned_weights), set(default_weights))


if __name__ == "__main__":
    unittest.main()

from dataclasses import dataclass

from tools.tuning.evaluate_weights import EvaluationMetrics, evaluate_samples


@dataclass(frozen=True)
class FakeSample:
    split: str
    game_id: str
    turn: int
    snake_id: str
    snake_name: str
    target_move: str
    board: object


class EvaluateWeightsTests(unittest.TestCase):
    def test_evaluate_samples_counts_matches_errors_and_timeouts(self) -> None:
        samples = [
            FakeSample("train", "g1", 0, "s1", "Alpha", "up", object()),
            FakeSample("train", "g1", 1, "s1", "Alpha", "left", object()),
            FakeSample("train", "g1", 2, "s1", "Alpha", "right", object()),
        ]

        def fake_minimax(board: object, snake_id: str, **kwargs: object) -> dict[str, object]:
            if kwargs["weights"]["marker"] == 1.0 and snake_id == "s1":
                if board is samples[0].board:
                    return {"move": "up", "timed_out": False}
                if board is samples[1].board:
                    return {"move": "right", "timed_out": True}
            raise RuntimeError("forced failure")

        metrics = evaluate_samples(
            samples,
            weights={"marker": 1.0},
            fixed_depth=3,
            time_budget_ms=5000,
            minimax_fn=fake_minimax,
        )

        self.assertIsInstance(metrics, EvaluationMetrics)
        self.assertEqual(metrics.samples, 3)
        self.assertEqual(metrics.matches, 1)
        self.assertEqual(metrics.mismatches, 1)
        self.assertEqual(metrics.errors, 1)
        self.assertEqual(metrics.timeouts, 1)
        self.assertAlmostEqual(metrics.accuracy, 1 / 3)
        self.assertAlmostEqual(metrics.score, (1 / 3) - 0.10 * (1 / 3) - 0.02 * (1 / 3))
