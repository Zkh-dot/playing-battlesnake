from __future__ import annotations

import time
import unittest
from unittest import mock

from fastapi.testclient import TestClient

import battlesnake.main as main
from battlesnake.main import app, move_deadline_ms, select_strategy
from battlesnake.strategies.base import Strategy
from battlesnake.strategies.first_safe import StrategyFirstSafe
from battlesnake.types import Move

LEGAL_MOVES = {"up", "down", "left", "right"}


def make_payload(snake_count: int = 3, timeout_ms: int | None = 500) -> dict:
    snakes = [
        {
            "id": f"snake-{index}",
            "name": f"snake-{index}",
            "health": 100,
            "body": [{"x": 2 * index, "y": 1}, {"x": 2 * index, "y": 0}],
        }
        for index in range(snake_count)
    ]
    game: dict = {"id": "test-game", "ruleset": {"name": "standard", "settings": {}}}
    if timeout_ms is not None:
        game["timeout"] = timeout_ms
    return {
        "game": game,
        "turn": 1,
        "board": {"height": 11, "width": 11, "food": [], "hazards": [], "snakes": snakes},
        "you": snakes[0],
    }


class SlowStrategy(Strategy):
    def decide(self, board, snake_id) -> Move:
        time.sleep(2.0)
        return Move.DOWN


class FailingStrategy(Strategy):
    def decide(self, board, snake_id) -> Move:
        raise RuntimeError("intentional test failure")


class InfoTests(unittest.TestCase):
    def test_info_reports_variant_and_revision(self) -> None:
        with mock.patch.dict("os.environ", {"STRATEGY_VARIANT": "first-safe"}):
            response = TestClient(app).get("/")
        self.assertEqual(response.status_code, 200)
        body = response.json()
        self.assertTrue(body["version"].startswith("0.1.0-dev+first-safe."))
        self.assertNotIn(body["version"].rsplit(".", 1)[-1], ("", "None"))

    def test_info_color_overridable_via_env(self) -> None:
        with mock.patch.dict("os.environ", {"SNAKE_COLOR": "#123456"}):
            response = TestClient(app).get("/")
        self.assertEqual(response.json()["color"], "#123456")

    def test_info_default_color_differs_from_production_snake(self) -> None:
        with mock.patch.dict("os.environ", clear=False):
            import os

            os.environ.pop("SNAKE_COLOR", None)
            response = TestClient(app).get("/")
        self.assertNotEqual(response.json()["color"], "#2563eb")


class VariantSelectionTests(unittest.TestCase):
    def _state(self, snake_count: int = 3):
        from battlesnake.types import GameState

        return GameState.model_validate(make_payload(snake_count))

    def test_default_variant_is_first_safe(self) -> None:
        with mock.patch.dict("os.environ", clear=False):
            import os

            os.environ.pop("STRATEGY_VARIANT", None)
            self.assertIsInstance(select_strategy(self._state()), StrategyFirstSafe)

    def test_unknown_variant_falls_back_to_first_safe(self) -> None:
        with mock.patch.dict("os.environ", {"STRATEGY_VARIANT": "no-such-variant"}):
            self.assertIsInstance(select_strategy(self._state()), StrategyFirstSafe)

    def test_registered_variant_is_selected(self) -> None:
        with mock.patch.dict(main.STANDARD_VARIANTS, {"slow-test": SlowStrategy}):
            with mock.patch.dict("os.environ", {"STRATEGY_VARIANT": "slow-test"}):
                self.assertIsInstance(select_strategy(self._state()), SlowStrategy)


class MoveDeadlineTests(unittest.TestCase):
    def test_deadline_subtracts_safety_margin(self) -> None:
        with mock.patch.dict("os.environ", {"MOVE_SAFETY_MARGIN_MS": "150"}):
            self.assertEqual(move_deadline_ms(500), 350)

    def test_deadline_has_minimum_floor(self) -> None:
        with mock.patch.dict("os.environ", {"MOVE_SAFETY_MARGIN_MS": "150"}):
            self.assertEqual(move_deadline_ms(100), 50)

    def test_deadline_uses_default_timeout_when_missing(self) -> None:
        with mock.patch.dict("os.environ", {"MOVE_SAFETY_MARGIN_MS": "150"}):
            self.assertEqual(move_deadline_ms(None), 350)


class MoveEndpointTests(unittest.TestCase):
    def test_move_returns_legal_move_for_standard_ffa(self) -> None:
        response = TestClient(app).post("/move", json=make_payload(snake_count=3))
        self.assertEqual(response.status_code, 200)
        self.assertIn(response.json()["move"], LEGAL_MOVES)

    def test_slow_strategy_falls_back_before_game_timeout(self) -> None:
        payload = make_payload(snake_count=3, timeout_ms=300)
        with mock.patch.dict(main.STANDARD_VARIANTS, {"slow-test": SlowStrategy}):
            with mock.patch.dict("os.environ", {"STRATEGY_VARIANT": "slow-test"}):
                started = time.monotonic()
                response = TestClient(app).post("/move", json=payload)
                elapsed_ms = (time.monotonic() - started) * 1000
        self.assertEqual(response.status_code, 200)
        self.assertIn(response.json()["move"], LEGAL_MOVES)
        self.assertLess(elapsed_ms, 300)

    def test_failing_strategy_falls_back_instead_of_500(self) -> None:
        with mock.patch.dict(main.STANDARD_VARIANTS, {"failing-test": FailingStrategy}):
            with mock.patch.dict("os.environ", {"STRATEGY_VARIANT": "failing-test"}):
                response = TestClient(app).post("/move", json=make_payload(snake_count=3))
        self.assertEqual(response.status_code, 200)
        self.assertIn(response.json()["move"], LEGAL_MOVES)


if __name__ == "__main__":
    unittest.main()
