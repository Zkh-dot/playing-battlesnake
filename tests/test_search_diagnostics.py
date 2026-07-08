from __future__ import annotations

import json
import unittest
from pathlib import Path

from battlesnake.battlesnake_native import Board, Coord, Snake, evaluate, minimax_diagnostics, minimax_move
from battlesnake.core.evaluation import WEIGHTS
from benchmarks.scenarios import build_board, get_scenario


EXPECTED_DIAGNOSTIC_KEYS = {
    "move",
    "score",
    "elapsed_ms",
    "parallel_mode",
    "parallel_workers_used",
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


ISSUE_11_SNAKE_ID = "gs_XyxHhq8f9MqXJkwSYdh37XbY"
ISSUE_30_FIXTURE_PATH = Path(__file__).resolve().parent / "fixtures" / "issue_30_endgame_positions.json"
ISSUE_30_EXPECTED_DEPTH_SCORE = {
    ("257c41ab-54c8-44e9-b589-70d2d0ff76a2", 481): (8, -992000.0),
    ("e1ca2e3f-a84a-4c83-83f4-8483f45fb564", 421): (9, -991000.0),
    ("978a236d-028c-4201-adb6-6950d755939a", 407): (5, -995000.0),
    ("cf5dd965-8afe-4cc1-a66e-76dffa72c39b", 360): (7, -993000.0),
}
TERMINAL_LOSS = -1000000.0
TERMINAL_WIN = 1000000.0
TERMINAL_SURVIVAL_STEP = 1000.0
TERMINAL_BAND = TERMINAL_SURVIVAL_STEP * 33


def _issue_11_turn_113_board() -> Board:
    return Board(
        width=11,
        height=11,
        ruleset_name="standard",
        hazard_damage=0,
        snakes=[
            Snake(
                id=ISSUE_11_SNAKE_ID,
                name="scvnak",
                health=92,
                body=[
                    Coord(9, 0),
                    Coord(8, 0),
                    Coord(7, 0),
                    Coord(6, 0),
                    Coord(5, 0),
                    Coord(4, 0),
                    Coord(4, 1),
                    Coord(4, 2),
                    Coord(3, 2),
                    Coord(3, 1),
                    Coord(2, 1),
                    Coord(2, 2),
                    Coord(1, 2),
                    Coord(1, 1),
                    Coord(0, 1),
                    Coord(0, 2),
                    Coord(0, 3),
                    Coord(1, 3),
                    Coord(2, 3),
                ],
                head=Coord(9, 0),
                length=19,
            ),
            Snake(
                id="gs_xfDkPgGMwBWQwkjSSfJd7vSF",
                name="test ml",
                health=86,
                body=[
                    Coord(8, 1),
                    Coord(8, 2),
                    Coord(8, 3),
                    Coord(8, 4),
                    Coord(8, 5),
                    Coord(8, 6),
                    Coord(7, 6),
                ],
                head=Coord(8, 1),
                length=7,
            ),
        ],
        food=[
            Coord(10, 10),
            Coord(8, 9),
            Coord(7, 4),
            Coord(9, 5),
            Coord(7, 8),
        ],
        hazards=[],
    )


def _board_from_issue_30_fixture(raw: dict[str, object]) -> Board:
    snakes = {
        str(snake["id"]): Snake(
            str(snake["id"]),
            str(snake["name"]),
            int(snake["health"]),
            [Coord(int(x), int(y)) for x, y in snake["body"]],
        )
        for snake in raw["snakes"]
    }
    return Board(
        width=int(raw["width"]),
        height=int(raw["height"]),
        snakes=snakes,
        food=[Coord(int(x), int(y)) for x, y in raw["food"]],
        hazards=[Coord(int(x), int(y)) for x, y in raw["hazards"]],
        ruleset_name=str(raw["ruleset_name"]),
        hazard_damage=int(raw["hazard_damage"]),
    )


def _issue_30_positions() -> list[dict[str, object]]:
    return json.loads(ISSUE_30_FIXTURE_PATH.read_text())["positions"]


def _adjust_terminal_child_score(score: float) -> float:
    if score >= TERMINAL_WIN - TERMINAL_BAND:
        return max(TERMINAL_WIN - TERMINAL_SURVIVAL_STEP * 32, score - TERMINAL_SURVIVAL_STEP)
    if score <= TERMINAL_LOSS + TERMINAL_BAND:
        return min(TERMINAL_LOSS + TERMINAL_SURVIVAL_STEP * 32, score + TERMINAL_SURVIVAL_STEP)
    return score


def _issue_30_root_worst_scores(board: Board, snake_id: str, depth: int) -> dict[str, float]:
    opponent_ids = [candidate for candidate in board.snakes if candidate != snake_id]
    if len(opponent_ids) != 1:
        raise AssertionError(f"expected one opponent, got {opponent_ids!r}")

    opponent_id = opponent_ids[0]
    scores: dict[str, float] = {}
    for own_move in board.safe_moves(snake_id):
        worst_reply = float("inf")
        for opponent_move in ("up", "down", "left", "right"):
            child = board.clone_and_apply({snake_id: own_move, opponent_id: opponent_move})
            if snake_id not in child.snakes or len(child.snakes) <= 1:
                child_score = float(evaluate(child, snake_id))
            else:
                child_score = float(
                    minimax_diagnostics(
                        child,
                        snake_id,
                        time_budget_ms=5000,
                        fixed_depth=max(depth - 1, 0),
                    )["score"]
                )
            worst_reply = min(worst_reply, _adjust_terminal_child_score(child_score))
        scores[own_move] = worst_reply
    return scores


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

    def test_minimax_diagnostics_accepts_weight_overrides(self) -> None:
        scenario = get_scenario("duel_open_7x7")
        diagnostics = minimax_diagnostics(
            build_board(scenario),
            scenario.snake_id,
            time_budget_ms=1000,
            fixed_depth=1,
            weights={"base": 999.0},
        )

        self.assertEqual(set(diagnostics), EXPECTED_DIAGNOSTIC_KEYS)
        self.assertIn(diagnostics["move"], {"up", "down", "left", "right"})

    def test_issue_11_turn_113_production_budget_avoids_fatal_corridor(self) -> None:
        result = minimax_diagnostics(
            _issue_11_turn_113_board(),
            ISSUE_11_SNAKE_ID,
            time_budget_ms=400,
        )

        self.assertEqual(result["move"], "up")
        self.assertNotEqual(result["move"], "right")

    def test_issue_11_turn_113_fixed_depth_models_opponent_corridor_block(self) -> None:
        result = minimax_diagnostics(
            _issue_11_turn_113_board(),
            ISSUE_11_SNAKE_ID,
            time_budget_ms=5000,
            fixed_depth=11,
        )

        self.assertEqual(result["completed_depth"], 11)
        self.assertEqual(result["move"], "up")

    def test_issue_30_forced_losses_keep_survival_horizon_in_score(self) -> None:
        for raw in _issue_30_positions():
            key = (str(raw["game_id"]), int(raw["turn"]))
            depth, expected_score = ISSUE_30_EXPECTED_DEPTH_SCORE[key]
            with self.subTest(game_id=key[0], turn=key[1]):
                board = _board_from_issue_30_fixture(raw)
                result = minimax_diagnostics(
                    board,
                    str(raw["snake_id"]),
                    time_budget_ms=5000,
                    fixed_depth=depth,
                )

                self.assertEqual(result["completed_depth"], depth)
                self.assertEqual(result["score"], expected_score)
                self.assertGreater(result["score"], TERMINAL_LOSS)

    def test_issue_30_selected_root_move_has_best_forced_loss_survival(self) -> None:
        for raw in _issue_30_positions():
            key = (str(raw["game_id"]), int(raw["turn"]))
            depth, _expected_score = ISSUE_30_EXPECTED_DEPTH_SCORE[key]
            with self.subTest(game_id=key[0], turn=key[1]):
                board = _board_from_issue_30_fixture(raw)
                snake_id = str(raw["snake_id"])
                result = minimax_diagnostics(board, snake_id, time_budget_ms=5000, fixed_depth=depth)
                root_scores = _issue_30_root_worst_scores(board, snake_id, depth)
                selected_score = root_scores[str(result["move"])]
                alternative_scores = [
                    score
                    for move, score in root_scores.items()
                    if move != result["move"]
                ]

                self.assertEqual(selected_score, max(root_scores.values()))
                self.assertTrue(alternative_scores)
                self.assertGreater(selected_score, max(alternative_scores))

    def test_terminal_survival_band_does_not_swallow_narrow_weight_heuristics(self) -> None:
        board = Board(
            width=7,
            height=7,
            snakes={
                "me": Snake("me", "me", 90, [Coord(1, 3), Coord(1, 2), Coord(1, 1)], length=3),
                "you": Snake("you", "you", 90, [Coord(5, 3), Coord(5, 2), Coord(5, 1)], length=3),
            },
            food=[Coord(3, 3)],
            ruleset_name="standard",
            hazard_damage=0,
        )
        narrow_terminal = {"terminal_win": 10000.0, "terminal_loss": -10000.0}
        wide_terminal = {"terminal_win": 1000000.0, "terminal_loss": -1000000.0}

        narrow = minimax_diagnostics(
            board,
            "me",
            time_budget_ms=1000,
            fixed_depth=1,
            weights=narrow_terminal,
        )
        wide = minimax_diagnostics(
            board,
            "me",
            time_budget_ms=1000,
            fixed_depth=1,
            weights=wide_terminal,
        )

        self.assertEqual(narrow["completed_depth"], 1)
        self.assertEqual(narrow["move"], wide["move"])
        self.assertAlmostEqual(float(narrow["score"]), float(wide["score"]), places=6)

    def test_opponent_pressure_weight_overrides_change_score(self) -> None:
        scenario = get_scenario("duel_open_7x7")
        default = minimax_diagnostics(
            build_board(scenario),
            scenario.snake_id,
            time_budget_ms=1000,
            fixed_depth=1,
        )
        pressured = minimax_diagnostics(
            build_board(scenario),
            scenario.snake_id,
            time_budget_ms=1000,
            fixed_depth=1,
            weights={
                "opponent_reachable_space": 10.0,
                "territory_delta": 10.0,
                "opponent_safe_moves": 50.0,
                "opponent_low_health_food_denial": 10.0,
            },
        )

        self.assertNotEqual(pressured["score"], default["score"])

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

    def test_parallel_mode_keyword_accepts_serial_mode(self) -> None:
        scenario = get_scenario("duel_open_7x7")
        result = minimax_diagnostics(
            build_board(scenario),
            scenario.snake_id,
            time_budget_ms=1000,
            fixed_depth=3,
            parallel_mode="serial",
        )

        self.assertEqual(result["completed_depth"], 3)
        self.assertEqual(result["parallel_mode"], 0)
        self.assertEqual(result["parallel_workers_used"], 1)

    def test_old_positional_weights_slot_still_accepts_weights(self) -> None:
        scenario = get_scenario("duel_open_7x7")
        result = minimax_diagnostics(
            build_board(scenario),
            scenario.snake_id,
            1000,
            3,
            1,
            1,
            1,
            {"health": 0.0},
        )

        self.assertEqual(result["completed_depth"], 3)
        self.assertEqual(result["parallel_mode"], 0)

    def test_unknown_parallel_mode_is_rejected(self) -> None:
        scenario = get_scenario("duel_open_7x7")

        with self.assertRaises(ValueError):
            minimax_diagnostics(
                build_board(scenario),
                scenario.snake_id,
                time_budget_ms=1000,
                fixed_depth=3,
                parallel_mode="threads_everywhere",
            )

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
