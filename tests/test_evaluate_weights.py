from __future__ import annotations

import io
import json
import sys
import tempfile
import unittest
from dataclasses import dataclass
from pathlib import Path
from unittest.mock import patch

from tools.tuning.evaluate_weights import EvaluationMetrics, evaluate_samples
from tools.tuning.search_weights import (
    DEFAULT_SEARCH_SPACE,
    main as search_weights_main,
    merge_candidate_weights,
    suggest_random_candidate,
)

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

class SearchWeightsTests(unittest.TestCase):
    def test_search_space_covers_opponent_pressure_keys(self) -> None:
        self.assertIn("opponent_reachable_space", DEFAULT_SEARCH_SPACE)
        self.assertIn("territory_delta", DEFAULT_SEARCH_SPACE)
        self.assertIn("opponent_safe_moves", DEFAULT_SEARCH_SPACE)
        self.assertIn("opponent_low_health_food_denial", DEFAULT_SEARCH_SPACE)

    def test_random_candidate_is_within_bounds_and_merge_preserves_defaults(self) -> None:
        candidate = suggest_random_candidate(seed=123)
        for key, value in candidate.items():
            lower, upper = DEFAULT_SEARCH_SPACE[key]
            self.assertGreaterEqual(value, lower)
            self.assertLessEqual(value, upper)

        merged = merge_candidate_weights({"base": 500.0, "opponent_reachable_space": 0.0}, candidate)
        self.assertEqual(merged["base"], 500.0)
        self.assertEqual(merged["opponent_reachable_space"], candidate["opponent_reachable_space"])

    def test_main_falls_back_to_random_search_when_optuna_is_missing(self) -> None:
        def blocked_import(name: str, *args: object, **kwargs: object) -> object:
            if name == "optuna":
                raise ImportError("missing optuna")
            return original_import(name, *args, **kwargs)

        original_import = __import__
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "best_weights.json"
            argv = [
                "search_weights",
                "--exports",
                "exports",
                "--output",
                str(output),
                "--trials",
                "1",
            ]
            with (
                patch.object(sys, "argv", argv),
                patch.object(sys, "stderr", io.StringIO()),
                patch.object(sys, "stdout", io.StringIO()),
                patch("builtins.__import__", side_effect=blocked_import),
                patch("tools.tuning.search_weights.load_weights", return_value={"base": 1.0}),
                patch("tools.tuning.search_weights.load_samples", return_value=[object()]),
                patch(
                    "tools.tuning.search_weights._run_random_search",
                    return_value={"base": 1.0, "opponent_reachable_space": 2.0},
                ) as random_search,
                patch("tools.tuning.search_weights._run_optuna_search") as optuna_search,
            ):
                self.assertEqual(search_weights_main(), 0)

        random_search.assert_called_once()
        self.assertEqual(random_search.call_args.kwargs["output"].name, "best_weights-trials.jsonl")
        optuna_search.assert_not_called()


if __name__ == "__main__":
    unittest.main()
