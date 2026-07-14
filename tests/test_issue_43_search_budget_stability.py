from __future__ import annotations

import json
from pathlib import Path

import pytest

from battlesnake.battlesnake_native import Board, Coord, Snake, minimax_diagnostics


FIXTURE_PATH = (
    Path(__file__).parent / "fixtures" / "issue_43_search_budget_positions.json"
)
FIXTURE = json.loads(FIXTURE_PATH.read_text(encoding="utf-8"))
POSITIONS = FIXTURE["positions"]
RISKY_POSITIONS = [position for position in POSITIONS if position["case"] == "risky_root"]
EQUIVALENT_POSITION = next(
    position
    for position in POSITIONS
    if position["case"] == "equivalent_maximal_frontier"
)
TINY_NODE_FALLBACK_POSITIONS = [
    position
    for position in RISKY_POSITIONS
    if position["evidence"]["game_id"]
    in {
        "8fd97d0d-6f20-436a-833c-062027a12617",
        "c7add22b-bc1e-443e-bad3-f271ac8886a1",
    }
]
PARTIAL_FRONTIER_POSITION = next(
    position
    for position in RISKY_POSITIONS
    if position["evidence"]["game_id"]
    == "0188bbac-32b6-4069-8bee-d24565c1cdd4"
)


def _board_from_fixture(raw: dict[str, object]) -> Board:
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


def _case_id(position: dict[str, object]) -> str:
    evidence = position["evidence"]
    return f'{evidence["game_id"][:8]}-t{evidence["turn"]}'


def _is_capacity_deficient_unknown(candidate: dict[str, object]) -> bool:
    return candidate["structural_proof"] == "unknown" and (
        int(candidate["post_move_length"]) <= 0
        or int(candidate["relaxed_static_capacity"])
        < int(candidate["post_move_length"])
    )


def _structurally_dominates(
    candidate: dict[str, object], incumbent: dict[str, object]
) -> bool:
    """Mirror the typed partial structural relation from CoreCompareRootCandidates."""
    return candidate["structural_proof"] == "safe" and (
        incumbent["structural_proof"] == "unsafe"
        or _is_capacity_deficient_unknown(incumbent)
    )


def _assert_complete_root_analysis(result: dict[str, object]) -> None:
    relevant = [
        candidate
        for candidate in result["root_candidates"].values()
        if candidate["safe_by_board_rules"] or candidate["allowed"]
    ]
    assert relevant
    for candidate in relevant:
        assert candidate["evaluated"] is True
        assert candidate["structural_proof"] != "not_analyzed"
        assert candidate["proof_cutoff"] != "not_analyzed"
        if candidate["allowed"]:
            assert candidate["minimax_score"] is not None
            assert candidate["minimax_outcome"] is not None
            assert candidate["minimax_bound"] is not None


def _assert_coherent_selected_diagnostics(result: dict[str, object]) -> None:
    selected = result["root_candidates"][result["move"]]
    assert int(result["completed_depth"]) >= 1
    assert int(result["max_depth_started"]) >= int(result["completed_depth"])
    assert result["selection_reason"] != "fallback"
    assert result["root_comparison_reason"] != "not_compared"
    assert selected["structural_proof"] != "not_analyzed"
    assert selected["proof_cutoff"] != "not_analyzed"
    assert selected["minimax_outcome"] in {"win", "draw", "unresolved", "loss"}
    assert selected["minimax_bound"] in {"exact", "lower", "upper"}
    assert result["score"] == selected["minimax_score"]


def _assert_risky_root_is_dominated(
    position: dict[str, object], result: dict[str, object]
) -> None:
    risky_move = str(position["evidence"]["recorded_risky_move"])
    candidates = result["root_candidates"]
    risky = candidates[risky_move]
    safe_alternatives = [
        candidate
        for move, candidate in candidates.items()
        if move != risky_move
        and candidate["allowed"]
        and candidate["structural_proof"] == "safe"
    ]

    assert risky["safe_by_board_rules"] is True
    assert risky["structural_proof"] in {"unsafe", "unknown"}
    assert safe_alternatives
    assert any(_structurally_dominates(safe, risky) for safe in safe_alternatives)
    assert result["move"] != risky_move
    assert result["root_candidates"][result["move"]]["structural_proof"] == "safe"


def _decision_fingerprint(result: dict[str, object]) -> dict[str, object]:
    result_fields = (
        "move",
        "score",
        "completed_depth",
        "max_depth_started",
        "timed_out",
        "node_budget",
        "node_budget_exhausted",
        "nodes",
        "root_allowed_mask",
        "root_policy_applied",
        "selection_reason",
        "root_comparison_reason",
        "root_analysis_nodes",
        "root_analysis_budget_ms",
        "search_reserved_ms",
        "root_move_scores",
    )
    candidate_fields = (
        "evaluated",
        "allowed",
        "safe_by_board_rules",
        "rejection_reason",
        "reply_outcomes",
        "alive_reply_mask",
        "alive_reply_count",
        "draw_reply_mask",
        "immediate_causes",
        "trap_status",
        "trap_horizon",
        "structural_proof",
        "proof_cutoff",
        "proof_horizon",
        "explored_states",
        "structural_capacity",
        "opponent_closure_considered",
        "post_move_length",
        "relaxed_static_capacity",
        "refutation_status",
        "minimax_score",
        "minimax_outcome",
        "minimax_terminal_distance",
        "minimax_bound",
        "minimax_cause",
    )

    # Explicitly include only decision state. Timings and unrelated future
    # diagnostics are observational and cannot make this contract flaky.
    return {
        **{field: result[field] for field in result_fields},
        "root_candidates": {
            move: {field: candidate[field] for field in candidate_fields}
            for move, candidate in result["root_candidates"].items()
        },
    }


@pytest.mark.parametrize("position", RISKY_POSITIONS, ids=_case_id)
def test_wall_time_budgets_preserve_structural_safety(
    position: dict[str, object],
) -> None:
    board = _board_from_fixture(position)
    snake_id = str(position["snake_id"])

    for time_budget_ms in (100, 200, 300):
        result = minimax_diagnostics(
            board,
            snake_id,
            time_budget_ms=time_budget_ms,
        )

        _assert_complete_root_analysis(result)
        _assert_coherent_selected_diagnostics(result)
        _assert_risky_root_is_dominated(position, result)


@pytest.mark.parametrize("position", RISKY_POSITIONS, ids=_case_id)
def test_node_budget_is_repeatable_and_preserves_structural_safety(
    position: dict[str, object],
) -> None:
    board = _board_from_fixture(position)
    snake_id = str(position["snake_id"])

    for node_budget in FIXTURE["node_budgets"]:
        results = [
            minimax_diagnostics(board, snake_id, node_budget=int(node_budget))
            for _ in range(3)
        ]

        assert all(result["nodes"] == node_budget for result in results)
        assert all(result["node_budget"] == node_budget for result in results)
        assert all(result["node_budget_exhausted"] is True for result in results)
        assert all(result["timed_out"] is False for result in results)
        assert all(
            result["selection_reason"] == "node_budget_best_so_far"
            for result in results
        )
        assert len({_json_fingerprint(result) for result in results}) == 1
        for result in results:
            _assert_complete_root_analysis(result)
            _assert_coherent_selected_diagnostics(result)
            _assert_risky_root_is_dominated(position, result)


@pytest.mark.parametrize("position", TINY_NODE_FALLBACK_POSITIONS, ids=_case_id)
def test_tiny_node_budget_uses_structurally_maximal_fallback(
    position: dict[str, object],
) -> None:
    risky_move = str(position["evidence"]["recorded_risky_move"])
    final_zero_depth_budget = {
        "8fd97d0d-6f20-436a-833c-062027a12617": 6,
        "c7add22b-bc1e-443e-bad3-f271ac8886a1": 7,
    }[str(position["evidence"]["game_id"])]

    for node_budget in range(1, final_zero_depth_budget + 1):
        result = minimax_diagnostics(
            _board_from_fixture(position),
            str(position["snake_id"]),
            node_budget=node_budget,
        )
        selected = result["root_candidates"][result["move"]]
        risky = result["root_candidates"][risky_move]

        assert result["nodes"] == node_budget
        assert result["completed_depth"] == 0
        assert result["node_budget_exhausted"] is True
        assert result["timed_out"] is False
        assert result["move"] == "right"
        assert selected["structural_proof"] == "safe"
        assert _structurally_dominates(selected, risky)
        assert all(
            candidate["evaluated"] is True
            and candidate["structural_proof"] != "not_analyzed"
            and candidate["proof_cutoff"] != "not_analyzed"
            for candidate in result["root_candidates"].values()
        )
        if "right" in result["root_move_scores"]:
            assert result["selection_reason"] == "node_budget_best_so_far"
            assert selected["minimax_score"] == result["root_move_scores"]["right"]
            assert selected["minimax_score"] == result["score"]
            assert selected["minimax_outcome"] is not None
            assert selected["minimax_bound"] in {"exact", "lower", "upper"}
        else:
            assert result["selection_reason"] == "allowed_fallback"
            assert result["root_comparison_reason"] == "structural_proof"
            assert selected["minimax_score"] is None
            assert selected["minimax_outcome"] is None
            assert selected["minimax_bound"] is None


def test_strict_minimax_tiny_node_budget_preserves_first_allowed_fallback() -> None:
    position = next(
        position
        for position in TINY_NODE_FALLBACK_POSITIONS
        if position["evidence"]["game_id"]
        == "c7add22b-bc1e-443e-bad3-f271ac8886a1"
    )

    result = minimax_diagnostics(
        _board_from_fixture(position),
        str(position["snake_id"]),
        node_budget=1,
        root_policy="strict_minimax",
    )

    assert result["root_policy_applied"] == "strict_minimax"
    assert result["nodes"] == 1
    assert result["completed_depth"] == 0
    assert result["node_budget_exhausted"] is True
    assert result["timed_out"] is False
    assert result["move"] == "down"
    assert result["selection_reason"] == "allowed_fallback"
    assert result["root_comparison_reason"] == "not_compared"
    assert result["root_move_scores"] == {}
    assert all(
        candidate["minimax_score"] is None
        and candidate["minimax_outcome"] is None
        and candidate["minimax_bound"] is None
        for candidate in result["root_candidates"].values()
    )


def test_tiny_node_budget_adopts_truthful_structural_frontier_partial() -> None:
    position = PARTIAL_FRONTIER_POSITION

    result = minimax_diagnostics(
        _board_from_fixture(position),
        str(position["snake_id"]),
        node_budget=4,
    )
    selected = result["root_candidates"]["down"]

    assert result["nodes"] == 4
    assert result["completed_depth"] == 0
    assert result["node_budget_exhausted"] is True
    assert result["timed_out"] is False
    assert result["move"] == "down"
    assert result["selection_reason"] == "node_budget_best_so_far"
    assert result["root_move_scores"] == {"down": selected["minimax_score"]}
    assert result["score"] == selected["minimax_score"]
    assert selected["structural_proof"] == "safe"
    assert selected["minimax_outcome"] == "unresolved"
    assert selected["minimax_bound"] in {"exact", "lower", "upper"}

def _json_fingerprint(result: dict[str, object]) -> str:
    return json.dumps(_decision_fingerprint(result), sort_keys=True)


def test_equivalent_frontier_node_budgets_are_repeatable_across_depths() -> None:
    position = EQUIVALENT_POSITION
    board = _board_from_fixture(position)
    snake_id = str(position["snake_id"])
    completed_depths: set[int] = set()
    selected_moves: set[str] = set()
    eligible_moves = set(position["eligible_equivalent_moves"])

    assert len(eligible_moves) >= 2

    for node_budget in position["node_budgets"]:
        results = [
            minimax_diagnostics(board, snake_id, node_budget=int(node_budget))
            for _ in range(3)
        ]

        assert len({_json_fingerprint(result) for result in results}) == 1
        result = results[0]
        assert result["nodes"] == node_budget
        assert result["node_budget_exhausted"] is True
        assert result["timed_out"] is False
        assert result["selection_reason"] == "node_budget_best_so_far"
        _assert_complete_root_analysis(result)
        _assert_coherent_selected_diagnostics(result)
        allowed_safe_moves = {
            move
            for move, candidate in result["root_candidates"].items()
            if candidate["allowed"] and candidate["structural_proof"] == "safe"
        }
        assert allowed_safe_moves == eligible_moves
        assert result["move"] in eligible_moves
        for move in eligible_moves:
            candidate = result["root_candidates"][move]
            assert candidate["allowed"] is True
            assert candidate["minimax_score"] is not None
            assert candidate["structural_proof"] == "safe"
        completed_depths.add(int(result["completed_depth"]))
        selected_moves.add(str(result["move"]))

    # The fixed work points deliberately prove the non-goal: deeper search can
    # change direction, but only within the known equivalent SAFE frontier.
    assert len(completed_depths) >= 2
    assert len(selected_moves) >= 2
