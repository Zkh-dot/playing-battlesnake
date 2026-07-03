from __future__ import annotations

import unittest

from battlesnake.battlesnake_native import Board, Coord, Snake, evaluate, minimax_diagnostics, minimax_move
from battlesnake.core.evaluation import WEIGHTS
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

    def test_make_unmake_matches_clone_search_result(self) -> None:
        for name in ("duel_open_7x7", "duel_tail_chase_trap", "royale_hazard_ring_duel"):
            with self.subTest(scenario=name):
                scenario = get_scenario(name)
                clone_result = minimax_diagnostics(
                    build_board(scenario),
                    scenario.snake_id,
                    time_budget_ms=1000,
                    fixed_depth=4,
                    enable_make_unmake=False,
                )
                in_place_result = minimax_diagnostics(
                    build_board(scenario),
                    scenario.snake_id,
                    time_budget_ms=1000,
                    fixed_depth=4,
                    enable_make_unmake=True,
                )

                self.assertEqual(in_place_result["move"], clone_result["move"])
                self.assertAlmostEqual(float(in_place_result["score"]), float(clone_result["score"]), places=6)
                self.assertLessEqual(in_place_result["clone_calls"], clone_result["clone_calls"])
                self.assertLessEqual(in_place_result["board_allocations"], clone_result["board_allocations"])

    def test_make_unmake_reduces_clone_counters(self) -> None:
        scenario = get_scenario("duel_open_7x7")
        clone_result = minimax_diagnostics(
            build_board(scenario),
            scenario.snake_id,
            time_budget_ms=1000,
            fixed_depth=4,
            enable_make_unmake=False,
        )
        in_place_result = minimax_diagnostics(
            build_board(scenario),
            scenario.snake_id,
            time_budget_ms=1000,
            fixed_depth=4,
            enable_make_unmake=True,
        )

        self.assertEqual(in_place_result["move"], clone_result["move"])
        self.assertEqual(in_place_result["completed_depth"], clone_result["completed_depth"])
        self.assertLess(in_place_result["clone_calls"], clone_result["clone_calls"])
        self.assertLess(in_place_result["board_allocations"], clone_result["board_allocations"])

    def test_make_unmake_fixed_depth_zero_flag_boundary_matches_clone_path(self) -> None:
        board = Board(
            width=7,
            height=7,
            snakes={
                "me": Snake("me", "me", 90, [Coord(3, 3), Coord(3, 2), Coord(3, 1)], length=3),
            },
            food=[Coord(5, 5)],
            ruleset_name="standard",
            hazard_damage=0,
        )
        clone_result = minimax_diagnostics(
            board,
            "me",
            time_budget_ms=5,
            fixed_depth=0,
            enable_make_unmake=False,
        )
        in_place_result = minimax_diagnostics(
            board,
            "me",
            time_budget_ms=5,
            fixed_depth=0,
            enable_make_unmake=True,
        )

        self.assertEqual(in_place_result["move"], clone_result["move"])
        self.assertEqual(in_place_result["completed_depth"], clone_result["completed_depth"])
        self.assertEqual(clone_result["clone_calls"], 0)
        self.assertEqual(clone_result["board_allocations"], 0)
        self.assertEqual(in_place_result["clone_calls"], 0)
        self.assertEqual(in_place_result["board_allocations"], 0)

    def test_make_unmake_handles_single_snake_board(self) -> None:
        board = Board(
            width=7,
            height=7,
            snakes={
                "me": Snake("me", "me", 90, [Coord(3, 3), Coord(3, 2), Coord(3, 1)], length=3),
            },
            food=[Coord(5, 5)],
            ruleset_name="standard",
            hazard_damage=0,
        )
        clone_result = minimax_diagnostics(
            board,
            "me",
            time_budget_ms=1000,
            fixed_depth=4,
            enable_make_unmake=False,
        )
        in_place_result = minimax_diagnostics(
            board,
            "me",
            time_budget_ms=1000,
            fixed_depth=4,
            enable_make_unmake=True,
        )

        self.assertEqual(in_place_result["move"], clone_result["move"])
        self.assertEqual(in_place_result["completed_depth"], clone_result["completed_depth"])
        self.assertEqual(in_place_result["clone_calls"], 0)
        self.assertEqual(in_place_result["board_allocations"], 0)

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

    def test_default_evaluation_weights_match_legacy_score(self) -> None:
        scenario = get_scenario("duel_open_7x7")
        board = build_board(scenario)

        self.assertAlmostEqual(
            evaluate(board, scenario.snake_id),
            evaluate(board, scenario.snake_id, WEIGHTS),
        )

    def test_evaluation_weight_overrides_are_used_by_evaluate_and_search(self) -> None:
        scenario = get_scenario("duel_open_7x7")
        board = build_board(scenario)

        default_score = evaluate(board, scenario.snake_id)
        no_health_score = evaluate(board, scenario.snake_id, {"health": 0.0})
        diagnostics = minimax_diagnostics(
            board,
            scenario.snake_id,
            time_budget_ms=1000,
            fixed_depth=2,
            weights={"health": 0.0},
        )

        self.assertNotEqual(default_score, no_health_score)
        self.assertEqual(set(diagnostics), EXPECTED_DIAGNOSTIC_KEYS)
        self.assertEqual(diagnostics["completed_depth"], 2)

    def test_duel_pressure_weight_overrides_are_used_by_evaluate_and_search(self) -> None:
        board = Board(
            width=7,
            height=7,
            snakes={
                "me": Snake("me", "me", 90, [Coord(2, 3), Coord(2, 2), Coord(2, 1)], length=3),
                "you": Snake("you", "you", 10, [Coord(5, 3), Coord(5, 2), Coord(5, 1)], length=3),
            },
            food=[Coord(3, 3)],
            ruleset_name="standard",
            hazard_damage=0,
        )
        weights = {
            "opponent_reachable_space": 1.0,
            "territory_delta": 1.0,
            "opponent_safe_moves": 1.0,
            "opponent_low_health_food_denial": 1.0,
        }

        default_score = evaluate(board, "me")
        pressure_score = evaluate(board, "me", weights)
        diagnostics = minimax_diagnostics(board, "me", time_budget_ms=1000, fixed_depth=2, weights=weights)

        self.assertNotEqual(default_score, pressure_score)
        self.assertEqual(set(diagnostics), EXPECTED_DIAGNOSTIC_KEYS)
        self.assertEqual(diagnostics["completed_depth"], 2)

    def test_invalid_evaluation_weight_type_is_rejected(self) -> None:
        scenario = get_scenario("duel_open_7x7")
        board = build_board(scenario)

        with self.assertRaises(TypeError):
            evaluate(board, scenario.snake_id, {"health": "high"})
        with self.assertRaises(KeyError):
            evaluate(board, scenario.snake_id, {"space": 10.0})


if __name__ == "__main__":
    unittest.main()
