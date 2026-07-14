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


def _global_structural_frontier(result: dict[str, object]) -> set[str]:
    searched = {
        move: candidate
        for move, candidate in result["root_candidates"].items()
        if candidate["allowed"] and candidate["minimax_score"] is not None
    }
    return {
        move
        for move, candidate in searched.items()
        if not any(
            other_move != move and _structurally_dominates(other, candidate)
            for other_move, other in searched.items()
        )
    }


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
    assert result["move"] in _global_structural_frontier(result)


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
    assert risky_move not in _global_structural_frontier(result)
    assert result["move"] != risky_move
    assert result["root_candidates"][result["move"]]["structural_proof"] == "safe"


def _decision_fingerprint(result: dict[str, object]) -> dict[str, object]:
    # Wall-clock measurements are observational. Everything else can affect or
    # explain the deterministic fixed-node decision and must repeat exactly.
    return {
        key: value
        for key, value in result.items()
        if key not in {"elapsed_ms", "root_analysis_elapsed_ms"}
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


def _json_fingerprint(result: dict[str, object]) -> str:
    return json.dumps(_decision_fingerprint(result), sort_keys=True)


def test_equivalent_frontier_node_budgets_are_repeatable_across_depths() -> None:
    position = EQUIVALENT_POSITION
    board = _board_from_fixture(position)
    snake_id = str(position["snake_id"])
    completed_depths: set[int] = set()

    for node_budget in FIXTURE["node_budgets"]:
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
        assert result["root_candidates"][result["move"]]["structural_proof"] == "safe"
        assert result["move"] in _global_structural_frontier(result)
        completed_depths.add(int(result["completed_depth"]))

    # Fixed work points exercise more than one completed iteration without
    # requiring equivalent frontier candidates to choose the same direction.
    assert len(completed_depths) >= 2
