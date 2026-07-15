from __future__ import annotations

import random
import json
from pathlib import Path
import unittest
from unittest.mock import patch

from battlesnake.battlesnake_native import Board, Coord, Snake
from benchmarks.scenarios import Scenario
from tools.tuning.compare_weights_matches import (
    MatchResult,
    MoveResult,
    _winner,
    experiment_schedule,
    percentile,
    play_match,
    summarize,
)

ROOT = Path(__file__).resolve().parents[1]


def _scenario(name: str = "fixture") -> Scenario:
    return Scenario(
        name=name,
        width=5,
        height=5,
        ruleset_name="standard",
        hazard_damage=0,
        snakes=(
            Snake("left", "left", 90, (Coord(1, 1),), Coord(1, 1), 1),
            Snake("right", "right", 90, (Coord(3, 1),), Coord(3, 1), 1),
        ),
        food=(),
        hazards=(),
    )


class CompareWeightsMatchesTests(unittest.TestCase):
    def test_schedule_is_seeded_paired_and_side_balanced(self) -> None:
        first = experiment_schedule(seed=46001, scenario_count=7)
        second = experiment_schedule(seed=46001, scenario_count=7)
        other = experiment_schedule(seed=46002, scenario_count=7)

        signature = lambda rows: [
            (row.pair, row.scenario.name, row.after_side,
             tuple((point.x, point.y) for snake in row.scenario.snakes for point in snake.body))
            for row in rows
        ]
        self.assertEqual(signature(first), signature(second))
        self.assertNotEqual(signature(first), signature(other))
        self.assertEqual(len(first), 14)
        for pair in range(7):
            rows = [row for row in first if row.pair == pair]
            self.assertEqual({row.after_side for row in rows}, {0, 1})
            self.assertEqual(len({row.scenario.name for row in rows}), 1)
        self.assertEqual(sum(row.after_side == 0 for row in first), 7)
        self.assertEqual(sum(row.after_side == 1 for row in first), 7)

    def test_percentile_uses_nearest_rank_semantics(self) -> None:
        values = list(range(1, 101))
        self.assertEqual(percentile(values, 0.50), 50.0)
        self.assertEqual(percentile(values, 0.95), 95.0)
        self.assertEqual(percentile(values, 0.99), 99.0)
        self.assertEqual(percentile([], 0.99), 0.0)

    def test_summary_recomputes_profile_and_paired_metrics(self) -> None:
        move = lambda profile, elapsed, risk=False, violation=False, timeout=False, error=None: MoveResult(
            profile=profile,
            turn=0,
            physical_side=0,
            move="up",
            elapsed_ms=elapsed,
            timed_out=timeout,
            structural_risk=risk,
            policy_violation=violation,
            error=error,
        )
        rows = [
            MatchResult(0, 0, "s0", 0, "after", 10, False, True, 0, 3, 0, 50,
                        (move("before", 10), move("after", 20, risk=True),)),
            MatchResult(1, 0, "s0", 1, "after", 12, False, True, 0, 4, 0, 60,
                        (move("before", 30, timeout=True), move("after", 40, violation=True),)),
            MatchResult(2, 1, "s1", 1, "before", 20, True, False, 5, 0, 70, 0,
                        (move("before", 50), move("after", 60, error="boom"),)),
            MatchResult(3, 1, "s1", 0, "draw", 25, True, True, 6, 6, 80, 80,
                        (move("before", 70), move("after", 80),)),
        ]

        summary = summarize(rows)
        self.assertEqual(summary["matches"], 4)
        self.assertEqual(summary["paired_outcomes"], {"after_sweeps": 1, "before_sweeps": 0, "split_pairs": 0, "pairs_with_draw": 1})
        self.assertEqual(summary["profiles"]["after"]["wins"], 2)
        self.assertEqual(summary["profiles"]["after"]["losses"], 1)
        self.assertEqual(summary["profiles"]["after"]["alive_at_cap"], 1)
        self.assertEqual(summary["profiles"]["after"]["search_errors"], 1)
        self.assertEqual(summary["profiles"]["after"]["structural_risk_selections"], 1)
        self.assertEqual(summary["profiles"]["after"]["policy_violations"], 1)
        self.assertEqual(summary["profiles"]["before"]["search_timeouts"], 1)
        self.assertEqual(summary["profiles"]["before"]["latency_ms"]["max"], 70.0)
        self.assertEqual(summary["profiles"]["after"]["physical_side_games"], {"0": 2, "1": 2})
        self.assertEqual(summary["paired_uncertainty"]["method"], "exact two-sided sign test on decisive non-split pairs")

    def test_winner_reports_mutual_death_as_draw(self) -> None:
        board = Board(3, 3, (), (), (), "standard", 0)
        self.assertEqual(_winner(board), "draw")

    def test_play_match_records_native_move_diagnostics_once_per_selection(self) -> None:
        diagnostics = {
            "move": "up",
            "elapsed_ms": 12.5,
            "timed_out": False,
            "root_candidates": {
                "up": {"structural_proof": "unknown", "relaxed_static_capacity": 0, "post_move_length": 1},
                "left": {"structural_proof": "safe", "alive_reply_count": 1},
            },
        }
        with (
            patch("tools.tuning.compare_weights_matches.minimax_diagnostics", return_value=diagnostics) as native,
            patch("tools.tuning.compare_weights_matches.audit_diagnostics") as audit,
            patch("tools.tuning.compare_weights_matches._complete_opponent_replies", return_value=("left",)),
        ):
            audit.return_value.violation = True
            result = play_match(
                match_index=0, pair=0, scenario=_scenario(), after_side=1,
                before_weights={}, after_weights={}, fixed_depth=1,
                time_budget_ms=1000, max_turns=1,
            )

        self.assertEqual(native.call_count, 2)
        self.assertEqual(audit.call_count, 2)
        self.assertEqual(len(result.moves), 2)
        self.assertTrue(all(move.structural_risk for move in result.moves))
        self.assertTrue(all(move.policy_violation for move in result.moves))
        self.assertTrue(all(move.elapsed_ms == 12.5 for move in result.moves))

    def test_audit_failure_is_recorded_without_changing_selected_move(self) -> None:
        diagnostics = {
            "move": "left", "elapsed_ms": 1.25, "timed_out": False,
            "root_candidates": {"left": {"structural_proof": "safe", "relaxed_static_capacity": 5, "post_move_length": 1}},
        }
        with (
            patch("tools.tuning.compare_weights_matches.minimax_diagnostics", return_value=diagnostics),
            patch("tools.tuning.compare_weights_matches._complete_opponent_replies", side_effect=ValueError("bad audit")),
        ):
            from tools.tuning.compare_weights_matches import _choose_move
            move = _choose_move(Board(5, 5, _scenario().snakes, (), (), "standard", 0), "before", {},
                                turn=0, physical_side=0, fixed_depth=1, time_budget_ms=300)
        self.assertEqual(move.move, "left")
        self.assertIsNone(move.error)
        self.assertEqual(move.audit_error, "ValueError: bad audit")

    def test_committed_ab_evidence_summary_recomputes_from_raw_rows(self) -> None:
        payload = json.loads((ROOT / "docs/evidence/issue-46-duel-weight-ab.json").read_text())
        results = [
            MatchResult(
                match=row["match"], pair=row["pair"], scenario=row["scenario"],
                after_side=row["after_side"], winner=row["winner"], turns=row["turns"],
                before_alive=row["before_alive"], after_alive=row["after_alive"],
                before_length=row["before_length"], after_length=row["after_length"],
                before_health=row["before_health"], after_health=row["after_health"],
                moves=tuple(MoveResult(**move) for move in row["moves"]),
            )
            for row in payload["results"]
        ]
        self.assertEqual(payload["settings"], {
            "seed": 46001, "scenario_count": 100, "paired_games_per_scenario": 2,
            "fixed_depth": 3, "time_budget_ms": 300, "max_turns": 200,
        })
        self.assertEqual(len(results), 200)
        self.assertEqual(summarize(results), payload["summary"])
        for profile in ("before", "after"):
            metrics = payload["summary"]["profiles"][profile]
            self.assertEqual(metrics["physical_side_games"], {"0": 100, "1": 100})
            self.assertEqual(metrics["search_errors"], 0)
            self.assertEqual(metrics["audit_errors"], 0)


if __name__ == "__main__":
    unittest.main()
