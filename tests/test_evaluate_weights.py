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
