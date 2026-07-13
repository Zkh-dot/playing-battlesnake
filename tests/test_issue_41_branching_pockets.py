from __future__ import annotations

import json
import time
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


def _board(
    width: int,
    height: int,
    me_body: list[tuple[int, int]],
    you_body: list[tuple[int, int]],
    *,
    food: list[tuple[int, int]] | None = None,
) -> Board:
    return Board(
        width=width,
        height=height,
        snakes={
            "me": Snake("me", "me", 90, [Coord(x, y) for x, y in me_body]),
            "you": Snake("you", "you", 90, [Coord(x, y) for x, y in you_body]),
        },
        food=[Coord(x, y) for x, y in food or []],
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


def test_shorter_opponent_arriving_first_can_close_future_cycle_doorway() -> None:
    board = _board(
        5,
        3,
        [(1, 0), (0, 0), (0, 1), (0, 2)],
        [(3, 1), (4, 1), (4, 2)],
    )

    candidate = _candidate(board, "right")

    assert candidate["opponent_closure_considered"] is True
    assert candidate["structural_proof"] != "safe"


def test_shorter_simultaneous_head_arrival_does_not_close_doorway() -> None:
    board = _board(
        5,
        3,
        [(1, 0), (0, 0), (0, 1), (0, 2)],
        [(3, 0), (4, 0), (4, 1)],
    )

    candidate = _candidate(board, "right")

    assert candidate["opponent_closure_considered"] is True
    assert candidate["reply_outcomes"]["left"] == "win"
    assert candidate["explored_states"] > 0


def test_shorter_opponent_body_blocks_cycle_entry_until_vacate() -> None:
    board = _board(
        5,
        4,
        [(1, 0), (0, 0), (0, 1), (0, 2), (0, 3)],
        [(3, 1), (2, 1), (2, 2), (3, 2)],
    )

    candidate = _candidate(board, "right")

    assert candidate["opponent_closure_considered"] is True
    assert candidate["structural_proof"] != "safe"


def test_shorter_opponent_growth_promotes_simultaneous_doorway_arrival() -> None:
    board = _board(
        5,
        3,
        [(1, 0), (0, 0), (0, 1), (0, 2)],
        [(1, 2), (1, 1), (2, 1)],
        food=[(2, 2)],
    )

    candidate = _candidate(board, "right")

    assert candidate["opponent_closure_considered"] is True
    assert candidate["structural_proof"] != "safe"


def test_two_reachable_meals_keep_shorter_tail_in_doorway() -> None:
    board = _board(
        5,
        4,
        [(1, 0), (0, 0), (0, 1), (0, 2), (0, 3)],
        [(4, 2), (4, 1), (3, 1), (2, 1)],
        food=[(3, 2), (2, 2)],
    )

    candidate = _candidate(board, "right")

    assert candidate["opponent_closure_considered"] is True
    assert candidate["structural_proof"] != "safe"


def test_equal_opponent_can_skip_optional_food_to_contest_earlier() -> None:
    board = _board(
        7,
        5,
        [(1, 0), (0, 0), (0, 1), (0, 2), (0, 3), (0, 4)],
        [(2, 2), (3, 2), (4, 2), (4, 1), (5, 1), (5, 0)],
        food=[(3, 4), (6, 3)],
    )

    candidate = _candidate(board, "right")

    assert candidate["opponent_closure_considered"] is True
    assert candidate["structural_proof"] != "safe"


def test_structural_dominance_preserves_guaranteed_immediate_win() -> None:
    board = _board(
        6,
        6,
        [(5, 1), (4, 1), (3, 1), (3, 0), (2, 0), (1, 0)],
        [(4, 0), (5, 0)],
    )

    result = minimax_diagnostics(board, "me", time_budget_ms=1000, fixed_depth=1)
    strict = minimax_diagnostics(
        board,
        "me",
        time_budget_ms=1000,
        fixed_depth=1,
        root_policy="strict_minimax",
    )
    winning = result["root_candidates"]["down"]

    assert set(winning["reply_outcomes"].values()) == {"win"}
    assert winning["structural_proof"] == "unknown"
    assert winning["allowed"] is True
    assert winning["rejection_reason"] == "none"
    assert result["move"] == "down"
    assert result["root_candidates"]["down"]["minimax_outcome"] == "win"
    assert strict["move"] == "down"
    assert strict["root_candidates"]["down"]["minimax_outcome"] == "win"


def test_structural_policy_preserves_mixed_immediate_win_draw() -> None:
    board = _board(
        5,
        5,
        [(1, 3), (2, 3), (2, 2), (2, 1)],
        [(0, 4), (0, 3), (0, 2), (1, 2)],
    )

    candidate = _candidate(board, "up")

    assert set(candidate["reply_outcomes"].values()) == {"win", "draw"}
    assert candidate["allowed"] is True
    assert candidate["rejection_reason"] == "none"


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
    expected_root_budget = budget_ms // 3

    assert result["move"] != bad_move
    assert result["completed_depth"] >= 1
    assert result["root_analysis_budget_ms"] == expected_root_budget
    assert result["search_reserved_ms"] == budget_ms - expected_root_budget
    assert bad["relaxed_static_capacity"] < bad["post_move_length"]
    assert bad["structural_proof"] != "safe"
    assert bad["proof_cutoff"] == "policy_sufficient"
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
    # Exact shorter-opponent closure pruning reduced this proof from 4356 to
    # 209 states before growth timing was added (236 now). The state count is
    # an implementation detail; the invariant is a completed SAFE proof that
    # did not fall through the board-derived resource guard.
    assert result["root_candidates"]["up"]["structural_proof"] == "safe"
    assert result["root_candidates"]["up"]["proof_cutoff"] != "resource_limit"
    assert strict["root_candidates"]["up"]["minimax_score"] == strict["root_candidates"][bad_move][
        "minimax_score"
    ]
    assert strict["root_candidates"][bad_move]["allowed"] is True


def test_strict_minimax_does_not_claim_opportunity_policy_sufficiency() -> None:
    position = _positions()[1]
    bad_move = str(position["evidence"]["recorded_bad_move"])

    result = minimax_diagnostics(
        _fixture_board(position),
        str(position["snake_id"]),
        time_budget_ms=1000,
        root_policy="strict_minimax",
    )
    bad = result["root_candidates"][bad_move]

    assert result["root_policy_applied"] == "strict_minimax"
    assert bad["allowed"] is True
    assert bad["rejection_reason"] == "none"
    assert bad["structural_proof"] == "unknown"
    assert bad["proof_cutoff"] == "horizon"
    assert bad["explored_states"] > 0


@pytest.mark.parametrize("attempt", range(3))
def test_large_board_root_phase_enforces_prefix_and_starts_search(attempt: int) -> None:
    del attempt
    board = _board(
        30,
        30,
        [(2, 2), (2, 1)],
        [(27, 27), (27, 28)],
    )
    budget_ms = 10

    started = time.perf_counter()
    result = minimax_diagnostics(board, "me", time_budget_ms=budget_ms)
    wall_elapsed_ms = (time.perf_counter() - started) * 1000.0
    deadline_candidates = [
        candidate
        for candidate in result["root_candidates"].values()
        if candidate["proof_cutoff"] == "deadline"
    ]

    assert 0.0 < result["root_analysis_elapsed_ms"] <= wall_elapsed_ms
    assert result["root_analysis_elapsed_ms"] < budget_ms
    assert result["max_depth_started"] == 1
    assert result["nodes"] > 0
    assert deadline_candidates
    assert all(candidate["structural_proof"] == "unknown" for candidate in deadline_candidates)


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


def test_static_cycle_is_not_safe_before_uncontested_repetition() -> None:
    board = _board(
        5,
        3,
        [(1, 0), (0, 0), (0, 1), (0, 2)],
        [(3, 1), (4, 1), (4, 2), (3, 2)],
    )

    candidate = _candidate(board, "right")

    assert candidate["opponent_closure_considered"] is True
    assert candidate["structural_proof"] != "safe"


def test_actual_root_reply_can_close_every_continuation() -> None:
    board = _board(
        5,
        5,
        [(3, 4), (3, 3), (3, 2), (3, 1), (4, 1), (4, 0), (3, 0), (2, 0)],
        [(1, 3), (2, 3), (2, 2), (1, 2), (1, 1), (1, 0), (0, 0), (0, 1)],
    )

    candidate = _candidate(board, "left")

    assert candidate["opponent_closure_considered"] is True
    assert candidate["structural_proof"] != "safe"


def test_established_region_is_cleared_after_topology_loss() -> None:
    board = _board(
        5,
        5,
        [(2, 2), (3, 2), (4, 2), (4, 3), (3, 3), (3, 4), (2, 4), (1, 4)],
        [(2, 0), (3, 0), (4, 0), (4, 1), (3, 1), (2, 1), (1, 1), (0, 1)],
    )

    candidate = _candidate(board, "up")

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
