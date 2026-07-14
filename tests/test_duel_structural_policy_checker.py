from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import subprocess
import sys

import pytest

from battlesnake.battlesnake_native import Board, Coord, Snake, minimax_diagnostics

import tools.check_duel_structural_policy as checker
from tools.check_duel_structural_policy import (
    audit_diagnostics,
    main,
    should_run_diagnostics,
)


def test_checker_module_exists() -> None:
    assert importlib.util.find_spec("tools.check_duel_structural_policy") is not None


def test_checker_python_entrypoint_exposes_help() -> None:
    completed = subprocess.run(
        [sys.executable, "tools/check_duel_structural_policy.py", "--help"],
        check=False,
        capture_output=True,
        text=True,
    )

    assert completed.returncode == 0, completed.stderr
    assert "--no-prefilter" in completed.stdout


def _candidate(
    *,
    capacity: int,
    length: int,
    proof: str,
    alive_replies: int = 1,
    allowed: bool = True,
    rejection_reason: str = "none",
    minimax_outcome: str = "unresolved",
    minimax_bound: str | None = None,
    minimax_score: float | None = None,
    minimax_terminal_distance: int = 0,
    reply_outcomes: dict[str, str] | None = None,
) -> dict[str, object]:
    return {
        "relaxed_static_capacity": capacity,
        "post_move_length": length,
        "structural_proof": proof,
        "alive_reply_count": alive_replies,
        "allowed": allowed,
        "rejection_reason": rejection_reason,
        "minimax_outcome": minimax_outcome,
        "minimax_bound": minimax_bound,
        "minimax_score": minimax_score,
        "minimax_terminal_distance": minimax_terminal_distance,
        "reply_outcomes": reply_outcomes or {},
    }


def test_pre_fix_policy_shape_is_a_violation() -> None:
    diagnostics = {
        "move": "left",
        "root_candidates": {
            "left": _candidate(capacity=5, length=8, proof="unknown"),
            "right": _candidate(capacity=12, length=8, proof="safe", alive_replies=2),
        },
    }

    audit = audit_diagnostics(diagnostics)

    assert audit.violation is True
    assert audit.selected_move == "left"
    assert audit.safe_alternatives == ("right",)
    assert audit.unknown_candidates == 1


def test_safe_or_structurally_dominated_selection_is_compliant() -> None:
    selected_safe = {
        "move": "right",
        "root_candidates": {
            "left": _candidate(
                capacity=5,
                length=8,
                proof="unsafe",
                allowed=False,
                rejection_reason="structurally_dominated",
            ),
            "right": _candidate(capacity=12, length=8, proof="safe"),
        },
    }

    audit = audit_diagnostics(selected_safe)

    assert audit.violation is False
    assert audit.unknown_candidates == 0


def test_unknown_is_counted_but_never_treated_as_safe() -> None:
    diagnostics = {
        "move": "left",
        "root_candidates": {
            "left": _candidate(capacity=5, length=8, proof="unknown"),
            "right": _candidate(capacity=12, length=8, proof="unknown", alive_replies=2),
        },
    }

    audit = audit_diagnostics(diagnostics)

    assert audit.violation is False
    assert audit.safe_alternatives == ()
    assert audit.unknown_candidates == 2


def test_all_draw_terminal_non_loss_is_preserved() -> None:
    diagnostics = {
        "move": "left",
        "complete_opponent_replies": ("up", "down"),
        "root_candidates": {
            "left": _candidate(
                capacity=0,
                length=8,
                proof="unsafe",
                alive_replies=0,
                minimax_outcome="draw",
                reply_outcomes={"up": "draw", "down": "draw"},
            ),
            "right": _candidate(capacity=12, length=8, proof="safe", alive_replies=2),
        },
    }

    assert audit_diagnostics(diagnostics).violation is False


def test_all_draw_terminal_non_loss_does_not_depend_on_minimax_completion() -> None:
    diagnostics = {
        "move": "left",
        "complete_opponent_replies": ("up", "down"),
        "root_candidates": {
            "left": _candidate(
                capacity=0,
                length=8,
                proof="unknown",
                alive_replies=7,
                minimax_outcome="unresolved",
                reply_outcomes={"up": "draw", "down": "draw"},
            ),
            "right": _candidate(capacity=12, length=8, proof="safe", alive_replies=2),
        },
    }

    assert audit_diagnostics(diagnostics).violation is False


@pytest.mark.parametrize(
    "reply_outcomes",
    [
        {"up": "win", "down": "win"},
        {"up": "win", "down": "draw"},
    ],
    ids=["all-win", "mixed-win-draw"],
)
def test_guaranteed_terminal_non_loss_is_not_a_violation(
    reply_outcomes: dict[str, str],
) -> None:
    diagnostics = {
        "move": "left",
        "complete_opponent_replies": tuple(reply_outcomes),
        "root_candidates": {
            "left": _candidate(
                capacity=1,
                length=8,
                proof="unknown",
                minimax_outcome="unresolved",
                reply_outcomes=reply_outcomes,
            ),
            "right": _candidate(capacity=12, length=8, proof="safe", alive_replies=1),
        },
    }

    assert audit_diagnostics(diagnostics).violation is False


def test_partial_terminal_non_loss_reply_set_remains_a_violation() -> None:
    diagnostics = {
        "move": "left",
        "complete_opponent_replies": ("up", "down"),
        "root_candidates": {
            "left": _candidate(
                capacity=1,
                length=8,
                proof="unknown",
                reply_outcomes={"up": "win"},
            ),
            "right": _candidate(capacity=12, length=8, proof="safe", alive_replies=1),
        },
    }

    assert audit_diagnostics(diagnostics).violation is True


def test_terminal_non_loss_without_reply_completeness_remains_a_violation() -> None:
    diagnostics = {
        "move": "left",
        "root_candidates": {
            "left": _candidate(
                capacity=1,
                length=8,
                proof="unknown",
                reply_outcomes={"up": "win", "down": "draw"},
            ),
            "right": _candidate(capacity=12, length=8, proof="safe", alive_replies=1),
        },
    }

    assert audit_diagnostics(diagnostics).violation is True


@pytest.mark.parametrize("nonterminal_outcome", ["both_alive", "loss"])
def test_incomplete_terminal_non_loss_remains_a_violation(
    nonterminal_outcome: str,
) -> None:
    diagnostics = {
        "move": "left",
        "root_candidates": {
            "left": _candidate(
                capacity=1,
                length=8,
                proof="unknown",
                reply_outcomes={
                    "up": "win",
                    "down": "draw",
                    "left": nonterminal_outcome,
                },
            ),
            "right": _candidate(capacity=12, length=8, proof="safe", alive_replies=1),
        },
    }

    assert audit_diagnostics(diagnostics).violation is True


def test_checker_matches_exact_six_by_six_all_win_root() -> None:
    board = _board(
        6,
        6,
        [(5, 1), (4, 1), (3, 1), (3, 0), (2, 0), (1, 0)],
        [(4, 0), (5, 0)],
    )
    diagnostics = minimax_diagnostics(board, "me", time_budget_ms=1000, fixed_depth=1)
    diagnostics["complete_opponent_replies"] = checker._complete_opponent_replies(
        board, "me"
    )
    selected = diagnostics["root_candidates"]["down"]

    assert diagnostics["move"] == "down"
    assert selected["relaxed_static_capacity"] < selected["post_move_length"]
    assert selected["structural_proof"] != "safe"
    assert set(selected["reply_outcomes"].values()) == {"win"}
    assert audit_diagnostics(diagnostics).violation is False


def test_selected_without_alive_reply_can_still_match_violation_predicate() -> None:
    diagnostics = {
        "move": "left",
        "root_candidates": {
            "left": _candidate(capacity=1, length=8, proof="unsafe", alive_replies=0),
            "right": _candidate(capacity=12, length=8, proof="safe", alive_replies=1),
        },
    }

    assert audit_diagnostics(diagnostics).violation is True


def test_strict_outcome_interval_dominance_is_independently_justified() -> None:
    diagnostics = {
        "move": "left",
        "root_candidates": {
            "left": _candidate(
                capacity=5,
                length=8,
                proof="unknown",
                minimax_outcome="unresolved",
                minimax_bound="exact",
                minimax_score=100.0,
            ),
            "right": _candidate(
                capacity=12,
                length=8,
                proof="safe",
                minimax_outcome="loss",
                minimax_bound="upper",
                minimax_score=-995000.0,
            ),
        },
    }

    audit = audit_diagnostics(diagnostics)

    assert audit.violation is False
    assert audit.justified_by_search is True
    assert audit.justification_layers == ("outcome_interval",)


def test_strict_numeric_interval_dominance_after_overlap_is_justified() -> None:
    diagnostics = {
        "move": "left",
        "root_candidates": {
            "left": _candidate(
                capacity=5,
                length=8,
                proof="unknown",
                minimax_outcome="loss",
                minimax_bound="exact",
                minimax_score=-991000.0,
            ),
            "right": _candidate(
                capacity=12,
                length=8,
                proof="safe",
                minimax_outcome="unresolved",
                minimax_bound="upper",
                minimax_score=-992000.0,
            ),
        },
    }

    audit = audit_diagnostics(diagnostics)

    assert audit.violation is False
    assert audit.justification_layers == ("numeric_interval",)


@pytest.mark.parametrize("bound", ["lower", "upper"])
@pytest.mark.parametrize("score", [float("nan"), float("inf"), -float("inf")])
def test_nonfinite_bound_has_no_numeric_interval(
    bound: str, score: float
) -> None:
    candidate = _candidate(
        capacity=12,
        length=8,
        proof="safe",
        minimax_bound=bound,
        minimax_score=score,
    )

    assert checker._numeric_interval(candidate) is None


def test_unresolved_heuristic_score_cannot_bypass_safe_structural_proof() -> None:
    diagnostics = {
        "move": "left",
        "root_candidates": {
            "left": _candidate(
                capacity=5,
                length=8,
                proof="unknown",
                minimax_outcome="unresolved",
                minimax_bound="exact",
                minimax_score=100.0,
            ),
            "right": _candidate(
                capacity=12,
                length=8,
                proof="safe",
                minimax_outcome="unresolved",
                minimax_bound="exact",
                minimax_score=0.0,
            ),
        },
    }

    audit = audit_diagnostics(diagnostics)

    assert audit.violation is True
    assert audit.justified_by_search is False
    assert audit.justification_layers == ()


def test_shorter_exact_loss_cannot_bypass_terminal_survival_with_numeric_score() -> None:
    diagnostics = {
        "move": "left",
        "root_candidates": {
            "left": _candidate(
                capacity=5,
                length=8,
                proof="unknown",
                minimax_outcome="loss",
                minimax_bound="exact",
                minimax_score=100.0,
                minimax_terminal_distance=8,
            ),
            "right": _candidate(
                capacity=12,
                length=8,
                proof="safe",
                minimax_outcome="loss",
                minimax_bound="exact",
                minimax_score=0.0,
                minimax_terminal_distance=9,
            ),
        },
    }

    audit = audit_diagnostics(diagnostics)

    assert audit.violation is True
    assert audit.justified_by_search is False
    assert audit.justification_layers == ()


def test_bounded_third_root_does_not_disable_exact_loss_terminal_survival() -> None:
    diagnostics = {
        "move": "left",
        "root_candidates": {
            "up": _candidate(
                capacity=12,
                length=8,
                proof="safe",
                minimax_outcome="loss",
                minimax_bound="exact",
                minimax_score=0.0,
                minimax_terminal_distance=10,
            ),
            "left": _candidate(
                capacity=5,
                length=8,
                proof="unknown",
                minimax_outcome="loss",
                minimax_bound="exact",
                minimax_score=100.0,
                minimax_terminal_distance=5,
            ),
            "right": _candidate(
                capacity=20,
                length=8,
                proof="safe",
                minimax_outcome="loss",
                minimax_bound="upper",
                minimax_score=50.0,
            ),
        },
    }

    audit = audit_diagnostics(diagnostics)

    assert audit.violation is True
    assert audit.justified_by_search is False
    assert audit.unjustified_safe_alternatives == ("right", "up")


def test_longer_active_exact_loss_invalidates_selected_terminal_survival() -> None:
    diagnostics = {
        "move": "left",
        "root_candidates": {
            "up": _candidate(
                capacity=12,
                length=8,
                proof="safe",
                minimax_outcome="loss",
                minimax_bound="exact",
                minimax_score=0.0,
                minimax_terminal_distance=5,
            ),
            "left": _candidate(
                capacity=5,
                length=8,
                proof="unknown",
                minimax_outcome="loss",
                minimax_bound="exact",
                minimax_score=100.0,
                minimax_terminal_distance=10,
            ),
            "right": _candidate(
                capacity=4,
                length=8,
                proof="unknown",
                minimax_outcome="loss",
                minimax_bound="exact",
                minimax_score=-100.0,
                minimax_terminal_distance=20,
            ),
        },
    }

    audit = audit_diagnostics(diagnostics)

    assert audit.violation is True
    assert audit.justified_by_search is False
    assert audit.unjustified_safe_alternatives == ("up",)
    assert audit.justification_layers == ()


@pytest.mark.parametrize("inactive_kind", ["disallowed", "unsearched"])
def test_inactive_longer_exact_loss_does_not_block_terminal_survival(
    inactive_kind: str,
) -> None:
    third = _candidate(
        capacity=4,
        length=8,
        proof="unknown",
        allowed=inactive_kind != "disallowed",
        minimax_outcome="loss",
        minimax_bound="exact",
        minimax_score=None if inactive_kind == "unsearched" else -100.0,
        minimax_terminal_distance=20,
    )
    diagnostics = {
        "move": "left",
        "root_candidates": {
            "up": _candidate(
                capacity=12,
                length=8,
                proof="safe",
                minimax_outcome="loss",
                minimax_bound="exact",
                minimax_score=0.0,
                minimax_terminal_distance=5,
            ),
            "left": _candidate(
                capacity=5,
                length=8,
                proof="unknown",
                minimax_outcome="loss",
                minimax_bound="exact",
                minimax_score=100.0,
                minimax_terminal_distance=10,
            ),
            "right": third,
        },
    }

    audit = audit_diagnostics(diagnostics)

    assert audit.violation is False
    assert audit.justified_by_search is True
    assert audit.unjustified_safe_alternatives == ()
    assert audit.justification_layers == ("terminal_survival",)


@pytest.mark.parametrize("inactive_kind", ["disallowed", "unsearched"])
def test_inactive_selected_root_cannot_claim_search_justification(
    inactive_kind: str,
) -> None:
    diagnostics = {
        "move": "left",
        "root_candidates": {
            "up": _candidate(
                capacity=12,
                length=8,
                proof="safe",
                minimax_outcome="loss",
                minimax_bound="exact",
                minimax_score=0.0,
                minimax_terminal_distance=5,
            ),
            "left": _candidate(
                capacity=5,
                length=8,
                proof="unknown",
                allowed=inactive_kind != "disallowed",
                minimax_outcome="loss",
                minimax_bound="exact",
                minimax_score=None if inactive_kind == "unsearched" else 100.0,
                minimax_terminal_distance=10,
            ),
            "right": _candidate(
                capacity=4,
                length=8,
                proof="unknown",
                minimax_outcome="loss",
                minimax_bound="exact",
                minimax_score=-100.0,
                minimax_terminal_distance=10,
            ),
        },
    }

    audit = audit_diagnostics(diagnostics)

    assert audit.violation is True
    assert audit.justified_by_search is False
    assert audit.unjustified_safe_alternatives == ("up",)
    assert audit.justification_layers == ()


@pytest.mark.parametrize("inactive_kind", ["disallowed", "unsearched"])
def test_inactive_safe_alternative_cannot_supply_search_justification(
    inactive_kind: str,
) -> None:
    diagnostics = {
        "move": "left",
        "root_candidates": {
            "left": _candidate(
                capacity=5,
                length=8,
                proof="unknown",
                minimax_outcome="loss",
                minimax_bound="exact",
                minimax_score=100.0,
                minimax_terminal_distance=10,
            ),
            "right": _candidate(
                capacity=12,
                length=8,
                proof="safe",
                allowed=inactive_kind != "disallowed",
                minimax_outcome="loss",
                minimax_bound="exact",
                minimax_score=None if inactive_kind == "unsearched" else 0.0,
                minimax_terminal_distance=5,
            ),
        },
    }

    audit = audit_diagnostics(diagnostics)

    assert audit.violation is True
    assert audit.justified_by_search is False
    assert audit.unjustified_safe_alternatives == ("right",)
    assert audit.justification_layers == ()


def test_touching_numeric_intervals_do_not_exempt_structural_violation() -> None:
    diagnostics = {
        "move": "left",
        "selection_reason": "timeout_best_so_far",
        "root_candidates": {
            "left": _candidate(
                capacity=5,
                length=8,
                proof="unknown",
                minimax_outcome="loss",
                minimax_bound="exact",
                minimax_score=-991000.0,
                minimax_terminal_distance=9,
            ),
            "right": _candidate(
                capacity=12,
                length=8,
                proof="safe",
                minimax_outcome="loss",
                minimax_bound="upper",
                minimax_score=-991000.0,
                minimax_terminal_distance=9,
            ),
        },
    }

    audit = audit_diagnostics(diagnostics)

    assert audit.violation is True
    assert audit.justified_by_search is False
    assert audit.unjustified_safe_alternatives == ("right",)


def test_all_exact_losses_require_strictly_longer_terminal_survival() -> None:
    diagnostics = {
        "move": "left",
        "root_candidates": {
            "left": _candidate(
                capacity=5,
                length=8,
                proof="unknown",
                minimax_outcome="loss",
                minimax_bound="exact",
                minimax_score=-991000.0,
                minimax_terminal_distance=9,
            ),
            "right": _candidate(
                capacity=12,
                length=8,
                proof="safe",
                minimax_outcome="loss",
                minimax_bound="exact",
                minimax_score=-992000.0,
                minimax_terminal_distance=8,
            ),
        },
    }

    audit = audit_diagnostics(diagnostics)

    assert audit.violation is False
    assert audit.justification_layers == ("terminal_survival",)


def test_corridor_guard_conflict_is_reported_separately() -> None:
    diagnostics = {
        "move": "left",
        "selection_reason": "corridor_guard",
        "root_candidates": {
            "left": _candidate(capacity=5, length=8, proof="unknown"),
            "right": _candidate(capacity=12, length=8, proof="safe"),
        },
    }

    audit = audit_diagnostics(diagnostics)

    assert audit.violation is False
    assert audit.post_search_override is True


def _board(
    width: int,
    height: int,
    me_body: list[tuple[int, int]],
    opponent_body: list[tuple[int, int]],
) -> Board:
    return Board(
        width,
        height,
        {
            "me": Snake("me", "scvnak", 90, [Coord(*point) for point in me_body]),
            "opponent": Snake(
                "opponent", "opponent", 90, [Coord(*point) for point in opponent_body]
            ),
        },
        [],
        [],
        ruleset_name="standard",
        hazard_damage=0,
    )


def test_prefilter_skips_only_when_static_lower_bounds_rule_out_deficiency() -> None:
    open_board = _board(7, 7, [(3, 3)], [(5, 5), (5, 4)])
    pocket_board = _board(
        5,
        5,
        [(2, 0), (2, 1), (2, 2), (1, 2), (0, 2), (0, 3), (1, 3), (1, 4), (0, 4)],
        [(4, 4), (4, 3), (4, 2)],
    )

    assert should_run_diagnostics(open_board, "me") is False
    assert should_run_diagnostics(pocket_board, "me") is True


def test_prefilter_keeps_no_alive_selected_candidate_with_one_alive_alternative() -> None:
    board = Board(
        3,
        3,
        {
            "me": Snake("me", "scvnak", 90, [Coord(0, 0)], length=10),
            "opponent": Snake(
                "opponent", "opponent", 90, [Coord(0, 1), Coord(0, 1)]
            ),
        },
        [],
        [],
        ruleset_name="standard",
        hazard_damage=0,
    )

    assert should_run_diagnostics(board, "me") is True


def test_prefilter_considers_every_candidate_allowed_by_a_diagnostics_record() -> None:
    board = _board(3, 3, [(0, 0)], [(0, 1), (0, 1)])

    # A synthetic diagnostics record can select an immediate-loss command even
    # though production normally rejects it.  The checker predicate does not
    # include that policy assumption, so the prefilter must fail open here.
    assert should_run_diagnostics(board, "me") is True


def test_prefilter_does_not_unblock_non_vacating_own_body() -> None:
    board = _board(
        7,
        7,
        [(3, 3), (3, 2), (3, 1)],
        [(6, 6), (6, 5)],
    )

    assert should_run_diagnostics(board, "me") is True


def test_committed_issue_positions_are_checker_compliant() -> None:
    fixture_path = Path(__file__).parent / "fixtures" / "issue_41_branching_pocket_positions.json"
    positions = json.loads(fixture_path.read_text(encoding="utf-8"))["positions"]

    for position in positions:
        board = Board(
            int(position["width"]),
            int(position["height"]),
            {
                snake["id"]: Snake(
                    snake["id"],
                    snake["name"],
                    snake["health"],
                    [Coord(x, y) for x, y in snake["body"]],
                )
                for snake in position["snakes"]
            },
            [Coord(x, y) for x, y in position["food"]],
            [Coord(x, y) for x, y in position["hazards"]],
            ruleset_name=position["ruleset_name"],
            hazard_damage=position["hazard_damage"],
        )
        diagnostics = minimax_diagnostics(
            board, position["snake_id"], time_budget_ms=100
        )

        assert audit_diagnostics(diagnostics).violation is False


def _raw_snake(
    snake_id: str, name: str, body: list[tuple[int, int]]
) -> dict[str, object]:
    return {
        "ID": snake_id,
        "Name": name,
        "Health": 90,
        "Death": None,
        "Body": [{"X": x, "Y": y} for x, y in body],
    }


def _write_export(path: Path, *, ruleset: str = "standard") -> None:
    me = _raw_snake("me", "scvnak", [(1, 1), (1, 0)])
    opponent = _raw_snake("opponent", "opponent", [(3, 3), (3, 2)])
    next_me = _raw_snake("me", "scvnak", [(2, 1), (1, 1)])
    next_opponent = _raw_snake("opponent", "opponent", [(2, 3), (3, 3)])
    export = {
        "game_id": path.stem,
        "game": {
            "ID": path.stem,
            "Width": 5,
            "Height": 5,
            "RulesetName": ruleset,
        },
        "frames": [
            {"Turn": 7, "Snakes": [me, opponent], "Food": [], "Hazards": []},
            {"Turn": 8, "Snakes": [next_me, next_opponent], "Food": [], "Hazards": []},
        ],
    }
    path.write_text(json.dumps(export), encoding="utf-8")


def _pre_fix_diagnostics(_board: Board, _snake_id: str, **_kwargs: object) -> dict[str, object]:
    return {
        "move": "left",
        "root_candidates": {
            "left": _candidate(capacity=1, length=2, proof="unknown"),
            "right": _candidate(capacity=20, length=2, proof="safe", alive_replies=2),
        },
    }


def _compliant_diagnostics(_board: Board, _snake_id: str, **_kwargs: object) -> dict[str, object]:
    return {
        "move": "right",
        "root_candidates": {
            "left": _candidate(
                capacity=1,
                length=2,
                proof="unknown",
                allowed=False,
                rejection_reason="structurally_dominated",
            ),
            "right": _candidate(capacity=20, length=2, proof="safe", alive_replies=2),
        },
    }


def test_scanner_json_reports_actual_move_unknowns_and_nonzero_exit(
    tmp_path: Path, capsys
) -> None:
    _write_export(tmp_path / "game-a.json")
    _write_export(tmp_path / "ignored-wrapped.json", ruleset="wrapped")

    exit_code = main(
        ["--export-root", str(tmp_path), "--budget-ms", "17", "--json", "--no-prefilter"],
        diagnostics_fn=_pre_fix_diagnostics,
    )
    output = json.loads(capsys.readouterr().out)

    assert exit_code == 1
    assert output["budget_ms"] == 17
    assert output["standard_duel_root_frames"] == 2
    assert output["diagnostics_root_frames"] == 2
    assert output["unknown_candidate_proofs"] == 2
    assert output["cutoff_candidate_counts"] == {"none": 2}
    assert output["violations"] == [
        {
            "actual_move": "right",
            "game_id": "game-a",
            "safe_alternatives": ["right"],
            "selected_move": "left",
            "turn": 7,
            "unjustified_safe_alternatives": ["right"],
        },
        {
            "actual_move": None,
            "game_id": "game-a",
            "safe_alternatives": ["right"],
            "selected_move": "left",
            "turn": 8,
            "unjustified_safe_alternatives": ["right"],
        },
    ]


def test_scanner_zero_exit_and_stable_text_summary(tmp_path: Path, capsys) -> None:
    _write_export(tmp_path / "game-a.json")

    exit_code = main(
        ["--export-root", str(tmp_path), "--budget-ms", "23", "--no-prefilter"],
        diagnostics_fn=_compliant_diagnostics,
    )

    assert exit_code == 0
    assert capsys.readouterr().out == (
        "duel structural policy audit: files_discovered=1 standard_duel_root_frames=2 "
        "prefiltered_root_frames=0 diagnostics_root_frames=2 "
        "unknown_candidate_proofs=2 cutoff_candidate_counts=none:2 "
        "justified_search_selections=0 comparator_violations=0 "
        "post_search_overrides=0 errors=0 budget_ms=23\n"
    )


def test_missing_actual_move_is_optional_evidence_not_a_skipped_root(
    tmp_path: Path, capsys
) -> None:
    path = tmp_path / "terminal.json"
    _write_export(path)
    export = json.loads(path.read_text(encoding="utf-8"))
    export["frames"][1]["Snakes"][0]["Death"] = {"Cause": "collision"}
    path.write_text(json.dumps(export), encoding="utf-8")

    exit_code = main(
        ["--export-root", str(tmp_path), "--json", "--no-prefilter"],
        diagnostics_fn=_pre_fix_diagnostics,
    )
    output = json.loads(capsys.readouterr().out)

    assert exit_code == 1
    assert output["diagnostics_root_frames"] == 1
    assert output["violations"][0]["actual_move"] is None


def test_prefilter_and_exhaustive_mode_report_same_violation(
    tmp_path: Path, capsys
) -> None:
    path = tmp_path / "pocket.json"
    _write_export(path)
    export = json.loads(path.read_text(encoding="utf-8"))
    export["frames"][0]["Snakes"] = [
        _raw_snake(
            "me",
            "scvnak",
            [(2, 0), (2, 1), (2, 2), (1, 2), (0, 2), (0, 3), (1, 3), (1, 4), (0, 4)],
        ),
        _raw_snake("opponent", "opponent", [(4, 4), (4, 3), (4, 2)]),
    ]
    export["frames"][1]["Snakes"] = [
        _raw_snake(
            "me",
            "scvnak",
            [(1, 0), (2, 0), (2, 1), (2, 2), (1, 2), (0, 2), (0, 3), (1, 3), (1, 4)],
        ),
        _raw_snake("opponent", "opponent", [(3, 4), (4, 4), (4, 3)]),
    ]
    path.write_text(json.dumps(export), encoding="utf-8")

    prefiltered_exit = main(
        ["--export-root", str(tmp_path), "--json"],
        diagnostics_fn=_pre_fix_diagnostics,
    )
    prefiltered = json.loads(capsys.readouterr().out)
    exhaustive_exit = main(
        ["--export-root", str(tmp_path), "--json", "--no-prefilter"],
        diagnostics_fn=_pre_fix_diagnostics,
    )
    exhaustive = json.loads(capsys.readouterr().out)

    assert prefiltered_exit == exhaustive_exit == 1
    assert prefiltered["violations"] == exhaustive["violations"]
    assert prefiltered["diagnostics_root_frames"] == 2


def test_self_collision_prefilter_matches_exhaustive_violation(
    tmp_path: Path, capsys
) -> None:
    path = tmp_path / "self-collision.json"
    _write_export(path)
    export = json.loads(path.read_text(encoding="utf-8"))
    export["game"]["Width"] = 7
    export["game"]["Height"] = 7
    export["frames"][0]["Snakes"] = [
        _raw_snake("me", "scvnak", [(3, 3), (3, 2), (3, 1)]),
        _raw_snake("opponent", "opponent", [(6, 6), (6, 5)]),
    ]
    export["frames"][1]["Snakes"] = [
        _raw_snake("me", "scvnak", [(4, 3), (3, 3), (3, 2)]),
        _raw_snake("opponent", "opponent", [(5, 6), (6, 6)]),
    ]
    path.write_text(json.dumps(export), encoding="utf-8")

    prefiltered_exit = main(
        ["--export-root", str(tmp_path), "--turn", "7", "--json"],
        diagnostics_fn=_pre_fix_diagnostics,
    )
    prefiltered = json.loads(capsys.readouterr().out)
    exhaustive_exit = main(
        ["--export-root", str(tmp_path), "--turn", "7", "--json", "--no-prefilter"],
        diagnostics_fn=_pre_fix_diagnostics,
    )
    exhaustive = json.loads(capsys.readouterr().out)

    assert prefiltered_exit == exhaustive_exit == 1
    assert prefiltered["violations"] == exhaustive["violations"]
    assert prefiltered["diagnostics_root_frames"] == 1


def test_missing_export_root_is_an_explicit_input_error(tmp_path: Path, capsys) -> None:
    exit_code = main(["--export-root", str(tmp_path / "missing"), "--json"])
    output = json.loads(capsys.readouterr().out)

    assert exit_code == 2
    assert output["error_count"] == 1
    assert output["errors"][0]["kind"] == "missing_export_root"


def test_invalid_json_and_malformed_replay_are_explicit_errors(
    tmp_path: Path, capsys
) -> None:
    (tmp_path / "invalid.json").write_text("{", encoding="utf-8")
    (tmp_path / "invalid-encoding.json").write_bytes(b"\xff")
    (tmp_path / "malformed.json").write_text(
        json.dumps({"game": {"Width": 5}, "frames": {}}), encoding="utf-8"
    )
    (tmp_path / "malformed-snake.json").write_text(
        json.dumps(
            {
                "game": {"Width": 5, "Height": 5, "RulesetName": "standard"},
                "frames": [{"Turn": 1, "Snakes": ["not-a-snake"]}],
            }
        ),
        encoding="utf-8",
    )

    exit_code = main(["--export-root", str(tmp_path), "--json"])
    output = json.loads(capsys.readouterr().out)

    assert exit_code == 2
    assert output["error_count"] == 4
    assert {error["kind"] for error in output["errors"]} == {
        "invalid_encoding",
        "invalid_json",
        "malformed_replay",
    }


def test_semantically_invalid_ruleset_values_are_structured_errors(
    tmp_path: Path, capsys
) -> None:
    invalid_hazard = tmp_path / "invalid-hazard.json"
    _write_export(invalid_hazard)
    hazard_export = json.loads(invalid_hazard.read_text(encoding="utf-8"))
    hazard_export["game"]["Ruleset"] = {
        "name": "standard",
        "hazardDamagePerTurn": "bad",
    }
    invalid_hazard.write_text(json.dumps(hazard_export), encoding="utf-8")

    invalid_name = tmp_path / "invalid-ruleset-name.json"
    _write_export(invalid_name)
    name_export = json.loads(invalid_name.read_text(encoding="utf-8"))
    del name_export["game"]["RulesetName"]
    name_export["game"]["Ruleset"] = {"name": ["standard"]}
    invalid_name.write_text(json.dumps(name_export), encoding="utf-8")

    exit_code = main(["--export-root", str(tmp_path), "--json"])
    output = json.loads(capsys.readouterr().out)

    assert exit_code == 2
    assert output["error_count"] == 2
    assert all(error["kind"] == "malformed_replay" for error in output["errors"])


def test_native_profile_runtime_error_is_structured_with_root_context(
    tmp_path: Path, capsys, monkeypatch
) -> None:
    _write_export(tmp_path / "game-a.json")

    def fail_profile(*_args: object, **_kwargs: object) -> object:
        raise RuntimeError("profile failed")

    monkeypatch.setattr(checker, "duel_root_profile", fail_profile)
    exit_code = main(
        ["--export-root", str(tmp_path), "--turn", "7", "--json"],
        diagnostics_fn=_compliant_diagnostics,
    )
    output = json.loads(capsys.readouterr().out)

    assert exit_code == 2
    assert output["errors"] == [
        {
            "detail": "profile failed",
            "game_id": "game-a",
            "kind": "root_processing_error",
            "path": str(tmp_path / "game-a.json"),
            "stage": "prefilter",
            "turn": 7,
        }
    ]


def test_text_errors_localize_each_root_and_processing_stage(
    tmp_path: Path, capsys, monkeypatch
) -> None:
    _write_export(tmp_path / "game-a.json")
    _write_export(tmp_path / "game-b.json")

    def fail_profile(*_args: object, **_kwargs: object) -> object:
        raise RuntimeError("profile failed")

    monkeypatch.setattr(checker, "duel_root_profile", fail_profile)
    exit_code = main(["--export-root", str(tmp_path), "--turn", "7"])
    lines = capsys.readouterr().out.splitlines()

    assert exit_code == 2
    assert lines[1:] == [
        (
            f"ERROR root_processing_error {tmp_path / 'game-a.json'} "
            "game_id=game-a turn=7 stage=prefilter: profile failed"
        ),
        (
            f"ERROR root_processing_error {tmp_path / 'game-b.json'} "
            "game_id=game-b turn=7 stage=prefilter: profile failed"
        ),
    ]


def test_native_diagnostics_runtime_error_is_structured_with_root_context(
    tmp_path: Path, capsys
) -> None:
    _write_export(tmp_path / "game-a.json")

    def fail_diagnostics(*_args: object, **_kwargs: object) -> object:
        raise RuntimeError("diagnostics failed")

    exit_code = main(
        ["--export-root", str(tmp_path), "--turn", "7", "--json", "--no-prefilter"],
        diagnostics_fn=fail_diagnostics,
    )
    output = json.loads(capsys.readouterr().out)

    assert exit_code == 2
    assert output["errors"][0]["game_id"] == "game-a"
    assert output["errors"][0]["turn"] == 7
    assert output["errors"][0]["stage"] == "diagnostics"
    assert output["errors"][0]["detail"] == "diagnostics failed"


def test_limit_counts_prefiltered_and_diagnostics_roots_without_off_by_one(
    tmp_path: Path, capsys
) -> None:
    path = tmp_path / "mixed.json"
    _write_export(path)
    export = json.loads(path.read_text(encoding="utf-8"))
    export["game"]["Width"] = 7
    export["game"]["Height"] = 7
    export["frames"] = [
        {
            "Turn": 1,
            "Snakes": [
                _raw_snake("me", "scvnak", [(3, 3)]),
                _raw_snake("opponent", "opponent", [(6, 6)]),
            ],
            "Food": [],
            "Hazards": [],
        },
        {
            "Turn": 2,
            "Snakes": [
                _raw_snake("me", "scvnak", [(4, 3)]),
                _raw_snake("opponent", "opponent", [(5, 6)]),
            ],
            "Food": [],
            "Hazards": [],
        },
        {
            "Turn": 3,
            "Snakes": [
                _raw_snake("me", "scvnak", [(3, 3), (3, 2), (3, 1)]),
                _raw_snake("opponent", "opponent", [(6, 6), (6, 5)]),
            ],
            "Food": [],
            "Hazards": [],
        },
    ]
    path.write_text(json.dumps(export), encoding="utf-8")

    calls: list[int] = []

    def diagnostics(*args: object, **kwargs: object) -> dict[str, object]:
        calls.append(1)
        return _compliant_diagnostics(*args, **kwargs)

    assert main(
        ["--export-root", str(tmp_path), "--limit", "2", "--json"],
        diagnostics_fn=diagnostics,
    ) == 0
    prefiltered = json.loads(capsys.readouterr().out)
    assert prefiltered["standard_duel_root_frames"] == 2
    assert prefiltered["prefiltered_root_frames"] == 2
    assert prefiltered["diagnostics_root_frames"] == 0
    assert calls == []

    assert main(
        ["--export-root", str(tmp_path), "--limit", "2", "--json", "--no-prefilter"],
        diagnostics_fn=diagnostics,
    ) == 0
    exhaustive = json.loads(capsys.readouterr().out)
    assert exhaustive["standard_duel_root_frames"] == 2
    assert exhaustive["prefiltered_root_frames"] == 0
    assert exhaustive["diagnostics_root_frames"] == 2
    assert len(calls) == 2
