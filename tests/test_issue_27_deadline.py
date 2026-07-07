from __future__ import annotations

import json
import time
import unittest
from pathlib import Path
from unittest import mock

import battlesnake.main as main
from battlesnake.battlesnake_native import minimax_diagnostics
from battlesnake.game import MOVE_DELTAS, board_from_game_state
from battlesnake.strategies.duel import StrategyDuel
from battlesnake.types import GameState
from benchmarks.battlesnake_payloads import payload_by_name


FIXTURE_PATH = Path(__file__).parent / "fixtures" / "issue_27_wall_timeout_positions.json"
LEGAL_MOVES = {"up", "down", "left", "right"}


def issue_positions() -> list[dict]:
    return json.loads(FIXTURE_PATH.read_text(encoding="utf-8"))


class Issue27DeadlineTests(unittest.TestCase):
    def test_duel_strategy_budget_is_clamped_to_request_timeout(self) -> None:
        state = GameState.model_validate(issue_positions()[0]["payload"])
        with mock.patch.dict(
            "os.environ",
            {"BATTLESNAKE_SEARCH_BUDGET_MS": "400", "MOVE_SAFETY_MARGIN_MS": "150"},
        ):
            strategy = main.select_strategy(state)

        self.assertIsInstance(strategy, StrategyDuel)
        self.assertEqual(strategy.time_budget_ms, 350)

    def test_wall_timeout_positions_return_expected_non_wall_moves(self) -> None:
        with mock.patch.dict(
            "os.environ",
            {"BATTLESNAKE_SEARCH_BUDGET_MS": "400", "MOVE_SAFETY_MARGIN_MS": "150"},
        ):
            for fixture in issue_positions():
                with self.subTest(position=fixture["id"]):
                    payload = fixture["payload"]
                    state = GameState.model_validate(payload)
                    started = time.monotonic()
                    response = main.move(state)
                    elapsed_ms = (time.monotonic() - started) * 1000

                    move = response["move"]
                    self.assertEqual(move, fixture["expected_move"])
                    self.assertNotEqual(move, fixture["previous_direction"])
                    self.assertLess(elapsed_ms, payload["game"]["timeout"])

                    head = payload["you"]["body"][0]
                    dx, dy = MOVE_DELTAS[move]
                    next_head = {"x": head["x"] + dx, "y": head["y"] + dy}
                    self.assertGreaterEqual(next_head["x"], 0)
                    self.assertGreaterEqual(next_head["y"], 0)
                    self.assertLess(next_head["x"], payload["board"]["width"])
                    self.assertLess(next_head["y"], payload["board"]["height"])

    def test_near_timeout_search_returns_a_legal_best_so_far_move(self) -> None:
        state = GameState.model_validate_json(payload_by_name("duel_center_pressure_11x11"))
        board = board_from_game_state(state)

        started = time.monotonic()
        diagnostics = minimax_diagnostics(board, state.you.id, time_budget_ms=1)
        elapsed_ms = (time.monotonic() - started) * 1000

        self.assertIn(diagnostics["move"], LEGAL_MOVES)
        self.assertLess(elapsed_ms, 100)
        self.assertGreaterEqual(diagnostics["max_depth_started"], diagnostics["completed_depth"])


if __name__ == "__main__":
    unittest.main()
