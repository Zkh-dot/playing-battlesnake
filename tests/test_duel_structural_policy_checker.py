from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import subprocess
import sys

from battlesnake.battlesnake_native import Board, Coord, Snake, minimax_diagnostics

from tools.check_duel_structural_policy import (
    audit_diagnostics,
    main,
    should_run_diagnostics,
)


def test_checker_module_exists() -> None:
    assert importlib.util.find_spec("tools.check_duel_structural_policy") is not None


def test_checker_is_directly_executable() -> None:
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


def test_guaranteed_draw_is_preserved() -> None:
    diagnostics = {
        "move": "left",
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
    open_board = _board(7, 7, [(1, 1), (1, 0)], [(5, 5), (5, 4)])
    pocket_board = _board(
        5,
        5,
        [(2, 0), (2, 1), (2, 2), (1, 2), (0, 2), (0, 3), (1, 3), (1, 4), (0, 4)],
        [(4, 4), (4, 3), (4, 2)],
    )

    assert should_run_diagnostics(open_board, "me") is False
    assert should_run_diagnostics(pocket_board, "me") is True


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
    assert output["standard_duel_frames"] == 1
    assert output["scanned_frames"] == 1
    assert output["unknown_candidates"] == 1
    assert output["cutoff_counts"] == {"none": 1}
    assert output["violations"] == [
        {
            "actual_move": "right",
            "game_id": "game-a",
            "safe_alternatives": ["right"],
            "selected_move": "left",
            "turn": 7,
        }
    ]


def test_scanner_zero_exit_and_stable_text_summary(tmp_path: Path, capsys) -> None:
    _write_export(tmp_path / "game-a.json")

    exit_code = main(
        ["--export-root", str(tmp_path), "--budget-ms", "23", "--no-prefilter"],
        diagnostics_fn=_compliant_diagnostics,
    )

    assert exit_code == 0
    assert capsys.readouterr().out == (
        "duel structural policy audit: files=1 standard_duel_frames=1 "
        "prefiltered=0 scanned=1 unknown_candidates=1 cutoffs=none:1 "
        "violations=0 budget_ms=23\n"
    )
