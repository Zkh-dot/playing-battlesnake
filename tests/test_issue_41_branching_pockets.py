from __future__ import annotations

import json
from pathlib import Path

import pytest

from battlesnake.battlesnake_native import Board, Coord, Snake, minimax_diagnostics


FIXTURE_PATH = Path(__file__).parent / "fixtures" / "issue_41_branching_pocket_positions.json"


def _positions() -> list[dict[str, object]]:
    return json.loads(FIXTURE_PATH.read_text(encoding="utf-8"))["positions"]


def _fixture_board(raw: dict[str, object]) -> Board:
    return Board(
        width=int(raw["width"]),
        height=int(raw["height"]),
        snakes={
            str(snake["id"]): Snake(
                str(snake["id"]),
                str(snake["name"]),
                int(snake["health"]),
                [Coord(int(x), int(y)) for x, y in snake["body"]],
            )
            for snake in raw["snakes"]
        },
        food=[Coord(int(x), int(y)) for x, y in raw["food"]],
        hazards=[Coord(int(x), int(y)) for x, y in raw["hazards"]],
        ruleset_name=str(raw["ruleset_name"]),
        hazard_damage=int(raw["hazard_damage"]),
    )


def _board(width: int, height: int, me_body: list[tuple[int, int]], you_body: list[tuple[int, int]]) -> Board:
    return Board(
        width=width,
        height=height,
        snakes={
            "me": Snake("me", "me", 90, [Coord(x, y) for x, y in me_body]),
            "you": Snake("you", "you", 90, [Coord(x, y) for x, y in you_body]),
        },
        food=[],
        hazards=[],
        ruleset_name="standard",
        hazard_damage=0,
    )


def _candidate(board: Board, move: str) -> dict[str, object]:
    result = minimax_diagnostics(board, "me", time_budget_ms=1000, fixed_depth=1)
    return result["root_candidates"][move]


def test_branching_pocket_proves_every_branch_dies() -> None:
    board = _board(
        5,
        5,
        [(2, 0), (2, 1), (2, 2), (1, 2), (0, 2), (0, 3), (1, 3), (1, 4), (0, 4)],
        [(4, 4), (4, 3), (4, 2)],
    )

    candidate = _candidate(board, "left")

    assert candidate["structural_proof"] == "unsafe"
    assert candidate["proof_cutoff"] == "dead_end"
    assert candidate["proof_horizon"] == 34
    assert candidate["explored_states"] > 1


def test_repeatable_loop_is_not_rejected_as_unsafe() -> None:
    board = _board(
        3,
        2,
        [(0, 0), (0, 1), (1, 1), (1, 0)],
        [(2, 1)],
    )

    candidate = _candidate(board, "right")

    assert candidate["structural_proof"] == "safe"
    assert candidate["proof_cutoff"] == "cycle"
    assert candidate["explored_states"] > 1


def test_equal_length_opponent_closes_root_doorway() -> None:
    board = _board(
        7,
        7,
        [(2, 3), (1, 3), (1, 2)],
        [(4, 3), (5, 3), (5, 2)],
    )

    candidate = _candidate(board, "right")

    assert candidate["opponent_closure_considered"] is True
    assert candidate["structural_proof"] != "safe"


def test_extended_horizon_proves_a_long_corridor_dead_end() -> None:
    board = _board(
        10,
        1,
        [(2, 0), (1, 0), (0, 0)],
        [(9, 0)],
    )

    candidate = _candidate(board, "right")

    assert candidate["structural_proof"] == "unsafe"
    assert candidate["proof_cutoff"] == "dead_end"


def test_equal_length_opponent_closure_prevents_horizon_false_safety() -> None:
    board = _board(
        5,
        1,
        [(1, 0), (0, 0)],
        [(4, 0), (3, 0)],
    )

    candidate = _candidate(board, "right")

    assert candidate["opponent_closure_considered"] is True
    assert candidate["structural_proof"] != "safe"


def test_bounded_rectangle_cycle_proves_capacity() -> None:
    board = _board(
        4,
        3,
        [(1, 0), (0, 0), (0, 1), (0, 2)],
        [(1, 1)],
    )

    candidate = _candidate(board, "right")

    assert candidate["structural_proof"] == "safe"
    assert candidate["proof_cutoff"] == "capacity"
    assert candidate["structural_capacity"] == 6


def test_distant_equal_length_opponent_does_not_preempt_bounded_cycle_capacity() -> None:
    board = _board(
        12,
        3,
        [(1, 0), (0, 0), (0, 1), (0, 2)],
        [(11, 2), (10, 2), (9, 2), (8, 2)],
    )

    candidate = _candidate(board, "right")

    assert candidate["opponent_closure_considered"] is True
    assert candidate["structural_proof"] == "safe"
    assert candidate["proof_cutoff"] == "capacity"
    assert candidate["structural_capacity"] == 6


@pytest.mark.parametrize("position", _positions(), ids=lambda raw: f"T{raw['evidence']['turn']}")
def test_replay_has_safe_full_body_certificate_but_bad_move_does_not(
    position: dict[str, object],
) -> None:
    bad_move = str(position["evidence"]["recorded_bad_move"])
    result = minimax_diagnostics(
        _fixture_board(position),
        str(position["snake_id"]),
        time_budget_ms=5000,
        fixed_depth=1,
    )
    bad = result["root_candidates"][bad_move]
    alternatives = [
        candidate
        for move, candidate in result["root_candidates"].items()
        if move != bad_move and candidate["alive_reply_count"] > 0
    ]

    assert bad["relaxed_static_capacity"] < bad["post_move_length"]
    assert bad["structural_proof"] != "safe"
    assert any(candidate["structural_proof"] == "safe" for candidate in alternatives)


@pytest.mark.parametrize("position", _positions(), ids=lambda raw: f"T{raw['evidence']['turn']}")
@pytest.mark.parametrize("budget_ms", [100, 200, 300])
def test_replay_branching_pocket_is_structurally_dominated(
    position: dict[str, object], budget_ms: int
) -> None:
    evidence = position["evidence"]
    bad_move = str(evidence["recorded_bad_move"])
    result = minimax_diagnostics(
        _fixture_board(position), str(position["snake_id"]), time_budget_ms=budget_ms
    )
    bad = result["root_candidates"][bad_move]
    allowed = [
        candidate
        for candidate in result["root_candidates"].values()
        if candidate["allowed"] and candidate["alive_reply_count"] > 0
    ]

    assert result["move"] != bad_move
    assert bad["relaxed_static_capacity"] < bad["post_move_length"]
    assert bad["structural_proof"] != "safe"
    assert bad["allowed"] is False
    assert bad["rejection_reason"] == "structurally_dominated"
    assert any(candidate["structural_proof"] == "safe" for candidate in allowed)


def test_t439_fixed_depth_uses_root_dominance_before_search_scores() -> None:
    position = _positions()[1]
    bad_move = str(position["evidence"]["recorded_bad_move"])
    result = minimax_diagnostics(
        _fixture_board(position),
        str(position["snake_id"]),
        time_budget_ms=5000,
        fixed_depth=3,
    )
    bad = result["root_candidates"][bad_move]
    strict = minimax_diagnostics(
        _fixture_board(position),
        str(position["snake_id"]),
        time_budget_ms=5000,
        fixed_depth=3,
        root_policy="strict_minimax",
    )

    assert result["completed_depth"] == 3
    assert result["move"] != bad_move
    assert bad["allowed"] is False
    assert bad["rejection_reason"] == "structurally_dominated"
    assert bad["minimax_score"] is None
    assert result["root_candidates"]["up"]["explored_states"] > 4096
    assert strict["root_candidates"]["up"]["minimax_score"] == strict["root_candidates"][bad_move][
        "minimax_score"
    ]
    assert strict["root_candidates"][bad_move]["allowed"] is True


def test_deadline_cutoff_remains_unknown() -> None:
    position = _positions()[1]
    result = minimax_diagnostics(
        _fixture_board(position),
        str(position["snake_id"]),
        time_budget_ms=1,
        fixed_depth=1,
    )

    assert result["root_candidates"]["up"]["structural_proof"] == "unknown"
    assert result["root_candidates"]["up"]["proof_cutoff"] == "deadline"


def test_capacity_deficit_history_cannot_restart_horizon_proof() -> None:
    board = _board(
        5,
        4,
        [(2, 3), (2, 2), (3, 2), (3, 1), (2, 1), (2, 0)],
        [(0, 2), (0, 1)],
    )

    candidate = _candidate(board, "right")

    assert not (
        candidate["structural_proof"] == "safe" and candidate["proof_cutoff"] == "horizon"
    )


def test_bounded_cycle_checks_opponent_arrival_through_full_perimeter() -> None:
    board = _board(
        10,
        3,
        [(1, 0), (0, 0), (0, 1), (0, 2)],
        [(6, 2), (7, 2), (8, 2), (8, 1)],
    )

    candidate = _candidate(board, "right")

    assert candidate["opponent_closure_considered"] is True
    assert candidate["proof_cutoff"] != "capacity"


def test_static_cyclic_region_does_not_bypass_contested_doorway() -> None:
    board = _board(
        5,
        3,
        [(1, 2), (0, 2), (0, 1), (0, 0), (1, 0), (2, 0)],
        [(3, 1), (3, 2), (4, 2), (4, 1), (4, 0), (3, 0)],
    )

    candidate = _candidate(board, "down")

    assert candidate["opponent_closure_considered"] is True
    assert candidate["structural_proof"] != "safe"


@pytest.mark.parametrize("position", _positions(), ids=lambda raw: f"T{raw['evidence']['turn']}")
def test_strict_minimax_preserves_replay_root_candidates(position: dict[str, object]) -> None:
    bad_move = str(position["evidence"]["recorded_bad_move"])
    result = minimax_diagnostics(
        _fixture_board(position),
        str(position["snake_id"]),
        time_budget_ms=5000,
        fixed_depth=1,
        root_policy="strict_minimax",
    )
    bad = result["root_candidates"][bad_move]

    assert result["root_policy_applied"] == "strict_minimax"
    assert bad["allowed"] is True
    assert bad["rejection_reason"] == "none"
