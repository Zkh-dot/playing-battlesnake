from __future__ import annotations

import unittest

from battlesnake.battlesnake_native import Board, Coord, Snake, minimax_diagnostics, minimax_move
from benchmarks.scenarios import build_board, get_scenario


EXPECTED_DIAGNOSTIC_KEYS = {
    "move",
    "score",
    "elapsed_ms",
    "completed_depth",
    "max_depth_started",
    "timed_out",
    "nodes",
    "leaf_evals",
    "clone_calls",
    "board_allocations",
    "safe_move_calls",
    "beta_cutoffs",
    "move_order_first_choice_cutoffs",
    "tt_probes",
    "tt_hits",
    "tt_exact_hits",
    "tt_lower_hits",
    "tt_upper_hits",
    "tt_cutoffs",
    "tt_stores",
    "tt_collisions",
}


class SearchDiagnosticsTests(unittest.TestCase):
    def test_minimax_diagnostics_shape(self) -> None:
        scenario = get_scenario("duel_open_7x7")
        board = build_board(scenario)

        diagnostics = minimax_diagnostics(board, scenario.snake_id, time_budget_ms=25)

        self.assertEqual(set(diagnostics), EXPECTED_DIAGNOSTIC_KEYS)
        self.assertIn(diagnostics["move"], {"up", "down", "left", "right"})
        self.assertIsInstance(diagnostics["score"], float)
        self.assertGreaterEqual(diagnostics["completed_depth"], 0)
        self.assertGreaterEqual(diagnostics["max_depth_started"], diagnostics["completed_depth"])
        self.assertGreater(diagnostics["nodes"], 0)
        self.assertGreater(diagnostics["leaf_evals"], 0)
        self.assertGreaterEqual(diagnostics["clone_calls"], 0)
        self.assertGreaterEqual(diagnostics["board_allocations"], 0)
        self.assertGreaterEqual(diagnostics["beta_cutoffs"], 0)
        self.assertGreaterEqual(diagnostics["elapsed_ms"], 0)
        self.assertIn(diagnostics["timed_out"], {True, False})

    def test_fixed_depth_diagnostics_are_deterministic_enough_for_regression_tests(self) -> None:
        scenario = get_scenario("duel_open_7x7")
        first = minimax_diagnostics(
            build_board(scenario),
            scenario.snake_id,
            time_budget_ms=1000,
            fixed_depth=3,
        )
        second = minimax_diagnostics(
            build_board(scenario),
            scenario.snake_id,
            time_budget_ms=1000,
            fixed_depth=3,
        )

        self.assertEqual(first["move"], second["move"])
        self.assertEqual(first["completed_depth"], 3)
        self.assertEqual(second["completed_depth"], 3)
        self.assertEqual(first["nodes"], second["nodes"])

    def test_placeholder_flags_keep_api_shape(self) -> None:
        scenario = get_scenario("duel_open_7x7")
        diagnostics = minimax_diagnostics(
            build_board(scenario),
            scenario.snake_id,
            time_budget_ms=1000,
            fixed_depth=2,
            enable_tt=False,
            enable_move_ordering=False,
            enable_make_unmake=False,
        )

        self.assertEqual(set(diagnostics), EXPECTED_DIAGNOSTIC_KEYS)
        self.assertEqual(diagnostics["completed_depth"], 2)
        self.assertEqual(diagnostics["tt_probes"], 0)
        self.assertEqual(diagnostics["tt_hits"], 0)
        self.assertEqual(diagnostics["tt_cutoffs"], 0)
        self.assertEqual(diagnostics["tt_stores"], 0)

    def test_tt_metrics_are_reported_when_enabled(self) -> None:
        scenario = get_scenario("duel_late_game_long_bodies")
        board = build_board(scenario)
        result = minimax_diagnostics(board, scenario.snake_id, time_budget_ms=1000, fixed_depth=4, enable_tt=True)

        self.assertGreaterEqual(result["tt_probes"], 0)
        self.assertGreaterEqual(result["tt_hits"], 0)
        self.assertGreaterEqual(result["tt_stores"], 0)
        self.assertGreater(result["tt_stores"], 0)

    def test_tt_is_active_at_deeper_fixed_depth_without_changing_move(self) -> None:
        scenario = get_scenario("duel_open_7x7")
        uncached = minimax_diagnostics(
            build_board(scenario),
            scenario.snake_id,
            time_budget_ms=1000,
            fixed_depth=6,
            enable_tt=False,
        )
        cached = minimax_diagnostics(
            build_board(scenario),
            scenario.snake_id,
            time_budget_ms=1000,
            fixed_depth=6,
            enable_tt=True,
        )

        self.assertEqual(cached["completed_depth"], 6)
        self.assertEqual(cached["move"], uncached["move"])
        self.assertGreater(cached["tt_probes"], 0)
        self.assertGreater(cached["tt_stores"], 0)

    def test_move_ordering_changes_cutoff_profile_without_changing_move(self) -> None:
        scenario = get_scenario("duel_center_pressure_11x11")
        ordered = minimax_diagnostics(
            build_board(scenario),
            scenario.snake_id,
            time_budget_ms=1000,
            fixed_depth=5,
            enable_move_ordering=True,
        )
        unordered = minimax_diagnostics(
            build_board(scenario),
            scenario.snake_id,
            time_budget_ms=1000,
            fixed_depth=5,
            enable_move_ordering=False,
        )

        self.assertEqual(ordered["move"], unordered["move"])
        self.assertLessEqual(ordered["nodes"], unordered["nodes"])
        ordered_cutoff_rate = ordered["move_order_first_choice_cutoffs"] / max(ordered["beta_cutoffs"], 1)
        unordered_cutoff_rate = unordered["move_order_first_choice_cutoffs"] / max(unordered["beta_cutoffs"], 1)
        self.assertGreaterEqual(ordered_cutoff_rate, unordered_cutoff_rate)

    def test_allocation_counters_are_low_enough_for_fixed_depth_after_workspace(self) -> None:
        scenario = get_scenario("duel_open_7x7")
        result = minimax_diagnostics(
            build_board(scenario),
            scenario.snake_id,
            time_budget_ms=1000,
            fixed_depth=4,
        )

        self.assertGreater(result["nodes"], 0)
        self.assertLessEqual(result["board_allocations"], result["nodes"])

    def test_trapped_opponent_is_treated_as_dead_branch_not_search_error(self) -> None:
        board = Board(
            width=4,
            height=4,
            snakes={
                "me": Snake("me", "me", 90, [Coord(3, 3), Coord(3, 2), Coord(2, 2)], length=3),
                "trapped": Snake(
                    "trapped",
                    "trapped",
                    90,
                    [Coord(0, 0), Coord(1, 0), Coord(1, 1), Coord(0, 1)],
                    length=4,
                ),
            },
            ruleset_name="constrictor",
            hazard_damage=0,
        )

        result = minimax_diagnostics(board, "me", time_budget_ms=1000, fixed_depth=2)

        self.assertEqual(result["completed_depth"], 2)
        self.assertIn(result["move"], {"up", "down", "left", "right"})

    def test_invalid_fixed_depth_is_rejected(self) -> None:
        scenario = get_scenario("duel_open_7x7")
        board = build_board(scenario)

        with self.assertRaises(ValueError):
            minimax_diagnostics(board, scenario.snake_id, fixed_depth=-1)
        with self.assertRaises(ValueError):
            minimax_diagnostics(board, scenario.snake_id, fixed_depth=33)

    def test_diagnostics_do_not_change_production_api(self) -> None:
        scenario = get_scenario("duel_open_7x7")
        board = build_board(scenario)

        move = minimax_move(board, scenario.snake_id, time_budget_ms=25)

        self.assertIn(move, {"up", "down", "left", "right"})


if __name__ == "__main__":
    unittest.main()
