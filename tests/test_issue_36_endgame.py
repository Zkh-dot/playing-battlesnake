from __future__ import annotations

import json
import unittest
from pathlib import Path

from battlesnake.battlesnake_native import minimax_diagnostics
from tests.test_search_diagnostics import _board_from_issue_30_fixture

FIXTURE_PATH = Path(__file__).resolve().parent / "fixtures" / "issue_36_endgame_positions.json"
PRODUCTION_LIKE_DEPTH = 11
GENEROUS_BUDGET_MS = 30000
STRICT_BAD_MOVE_CASES = {
    ("ebaca2a0-0f2a-411d-87a1-e2766d7daa50", 450),
    ("9f1b79ed-9fbf-4732-aabb-254c0fb3fd6c", 290),
}


def _positions() -> list[dict[str, object]]:
    return json.loads(FIXTURE_PATH.read_text())["positions"]


class Issue36EndgameTests(unittest.TestCase):
    def test_critical_turns_avoid_known_bad_moves(self) -> None:
        checked: set[tuple[str, int]] = set()
        for raw in _positions():
            if "bad_move" not in raw:
                continue
            checked.add((str(raw["game_id"]), int(raw["turn"])))
            with self.subTest(game_id=raw["game_id"], turn=raw["turn"]):
                board = _board_from_issue_30_fixture(raw)
                result = minimax_diagnostics(
                    board,
                    str(raw["snake_id"]),
                    time_budget_ms=GENEROUS_BUDGET_MS,
                    fixed_depth=PRODUCTION_LIKE_DEPTH,
                )

                self.assertEqual(result["completed_depth"], PRODUCTION_LIKE_DEPTH)
                self.assertNotEqual(result["move"], raw["bad_move"])
        self.assertEqual(checked, STRICT_BAD_MOVE_CASES)

    def test_bad_moves_score_strictly_below_chosen_move(self) -> None:
        for raw in _positions():
            if "bad_move" not in raw:
                continue
            with self.subTest(game_id=raw["game_id"], turn=raw["turn"]):
                board = _board_from_issue_30_fixture(raw)
                result = minimax_diagnostics(
                    board,
                    str(raw["snake_id"]),
                    time_budget_ms=GENEROUS_BUDGET_MS,
                    fixed_depth=PRODUCTION_LIKE_DEPTH,
                )
                root_scores = result["root_move_scores"]

                self.assertIn(str(raw["bad_move"]), root_scores)
                self.assertLess(
                    root_scores[str(raw["bad_move"])],
                    root_scores[str(result["move"])],
                )

    def test_b085baae_turn_344_terminal_loss_tie_prefers_open_corridor(self) -> None:
        raw = next(
            position
            for position in _positions()
            if str(position["game_id"]).startswith("b085baae") and int(position["turn"]) == 344
        )
        board = _board_from_issue_30_fixture(raw)
        result = minimax_diagnostics(
            board,
            str(raw["snake_id"]),
            time_budget_ms=GENEROUS_BUDGET_MS,
            fixed_depth=PRODUCTION_LIKE_DEPTH,
        )
        root_scores = result["root_move_scores"]

        self.assertEqual(result["completed_depth"], PRODUCTION_LIKE_DEPTH)
        self.assertEqual(root_scores["up"], root_scores["left"])
        self.assertEqual(result["move"], "up")
        self.assertNotEqual(result["move"], "left")


if __name__ == "__main__":
    unittest.main()
