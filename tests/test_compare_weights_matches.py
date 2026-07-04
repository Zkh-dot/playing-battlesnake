from __future__ import annotations

import unittest
from unittest.mock import patch

from battlesnake.battlesnake_native import Board, Coord, Snake
from benchmarks.scenarios import Scenario
from tools.tuning.compare_weights_matches import _winner, play_match


class CompareWeightsMatchesTests(unittest.TestCase):
    def test_winner_reports_mutual_death_as_draw(self) -> None:
        scenario = Scenario(
            name="mutual",
            width=3,
            height=3,
            ruleset_name="standard",
            hazard_damage=0,
            snakes=(),
            food=(),
            hazards=(),
        )
        board = Board(
            scenario.width,
            scenario.height,
            (),
            scenario.food,
            scenario.hazards,
            scenario.ruleset_name,
            scenario.hazard_damage,
        )

        self.assertEqual(_winner(board), "draw")

    def test_play_match_records_winner_on_final_allowed_turn(self) -> None:
        scenario = Scenario(
            name="final-turn-kill",
            width=3,
            height=3,
            ruleset_name="standard",
            hazard_damage=0,
            snakes=(
                Snake("left", "left", 90, (Coord(1, 1),), Coord(1, 1), 1),
                Snake("right", "right", 90, (Coord(2, 1),), Coord(2, 1), 1),
            ),
            food=(),
            hazards=(),
        )

        def choose_move(board: object, snake_id: str, weights: dict[str, float], **kwargs: object) -> tuple[str, bool]:
            return ("up" if snake_id == "before" else "right"), False

        with patch("tools.tuning.compare_weights_matches._choose_move", side_effect=choose_move):
            result = play_match(
                match_index=0,
                scenario=scenario,
                after_side=1,
                before_weights={},
                after_weights={},
                fixed_depth=1,
                time_budget_ms=1000,
                max_turns=1,
            )

        self.assertEqual(result.turns, 1)
        self.assertEqual(result.winner, "before")
        self.assertTrue(result.before_alive)
        self.assertFalse(result.after_alive)


if __name__ == "__main__":
    unittest.main()
