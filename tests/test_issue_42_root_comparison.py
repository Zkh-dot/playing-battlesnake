from __future__ import annotations

import json
from pathlib import Path

from battlesnake.battlesnake_native import Board, Coord, Snake, minimax_diagnostics, reachable_space
from benchmarks.scenarios import build_board, get_scenario


FIXTURE_PATH = Path(__file__).parent / "fixtures" / "issue_41_branching_pocket_positions.json"


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


def _board(
    width: int,
    height: int,
    me_body: list[tuple[int, int]],
    you_body: list[tuple[int, int]],
) -> Board:
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


def _center_only_weights() -> dict[str, float]:
    positional_terms = (
        "base",
        "health",
        "length",
        "reachable_space",
        "safe_moves",
        "food",
        "low_health_food",
        "low_health_threshold",
        "hazard_damage",
        "hazard",
        "length_advantage",
        "adjacent_equal_or_longer_penalty",
        "adjacent_shorter_bonus",
        "opponent_reachable_space",
        "territory_delta",
        "opponent_safe_moves",
        "opponent_low_health_food_denial",
    )
    weights = {term: 0.0 for term in positional_terms}
    weights["center"] = 1.0
    return weights


def _assert_selected_value_is_coherent(result: dict[str, object]) -> None:
    selected = result["root_candidates"][result["move"]]
    assert result["score"] == selected["minimax_score"]
    assert selected["minimax_outcome"] is not None
    assert selected["minimax_bound"] is not None


def _selected_tag(result: dict[str, object]) -> tuple[object, ...]:
    selected = result["root_candidates"][result["move"]]
    return (
        result["move"],
        result["score"],
        selected["minimax_outcome"],
        selected["minimax_bound"],
        tuple(selected["minimax_cause"] or ()),
        selected["minimax_terminal_distance"],
        result["root_comparison_reason"],
    )


def test_t169_searches_deficient_root_then_structural_layer_selects_safe_root() -> None:
    position = json.loads(FIXTURE_PATH.read_text(encoding="utf-8"))["positions"][0]

    result = minimax_diagnostics(
        _board_from_fixture(position),
        str(position["snake_id"]),
        time_budget_ms=5000,
        fixed_depth=1,
        root_policy="standard_ladder_opportunity",
        weights=_center_only_weights(),
    )
    deficient = result["root_candidates"]["down"]
    safe = result["root_candidates"]["left"]

    assert deficient["relaxed_static_capacity"] < deficient["post_move_length"]
    assert deficient["structural_proof"] == "unknown"
    assert safe["structural_proof"] == "safe"
    assert deficient["allowed"] is True
    assert deficient["minimax_score"] is not None
    assert safe["minimax_score"] is not None
    assert deficient["minimax_outcome"] == "unresolved"
    assert safe["minimax_outcome"] == "unresolved"
    assert result["root_candidates"][result["move"]]["structural_proof"] == "safe"
    assert result["move"] != "down"
    assert result["root_comparison_reason"] == "stable_direction"
    _assert_selected_value_is_coherent(result)


def test_single_safe_frontier_reports_structural_proof() -> None:
    position = json.loads(FIXTURE_PATH.read_text(encoding="utf-8"))["positions"][1]

    result = minimax_diagnostics(
        _board_from_fixture(position),
        str(position["snake_id"]),
        time_budget_ms=5000,
        fixed_depth=1,
    )

    evaluated = [
        candidate
        for candidate in result["root_candidates"].values()
        if candidate["minimax_score"] is not None
    ]
    assert sum(candidate["structural_proof"] == "safe" for candidate in evaluated) == 1
    assert any(
        candidate["structural_proof"] == "unknown"
        and candidate["relaxed_static_capacity"] < candidate["post_move_length"]
        for candidate in evaluated
    )
    assert result["move"] == "up"
    assert result["root_comparison_reason"] == "structural_proof"
    _assert_selected_value_is_coherent(result)


def test_t169_default_weights_preserve_move_and_report_actual_decisive_layer() -> None:
    position = json.loads(FIXTURE_PATH.read_text(encoding="utf-8"))["positions"][0]
    board = _board_from_fixture(position)
    snake_id = str(position["snake_id"])

    results = [
        minimax_diagnostics(
            board,
            snake_id,
            time_budget_ms=5000,
            fixed_depth=2,
            enable_tt=False,
            enable_move_ordering=ordering,
        )
        for ordering in (False, True, False, True)
    ]

    assert {result["move"] for result in results} == {"left"}
    assert {result["root_comparison_reason"] for result in results} == {"previous_pv", "search_bound"}
    assert {
        tuple(
            (move, candidate["minimax_bound"])
            for move, candidate in result["root_candidates"].items()
            if candidate["minimax_score"] is not None
        )
        for result in results
    } == {
        (("up", "exact"), ("down", "exact"), ("left", "upper")),
        (("up", "upper"), ("down", "upper"), ("left", "exact")),
    }
    for result in results:
        _assert_selected_value_is_coherent(result)


def test_t169_production_budgets_never_select_deficient_root_and_repeat_coherently() -> None:
    position = json.loads(FIXTURE_PATH.read_text(encoding="utf-8"))["positions"][0]
    board = _board_from_fixture(position)
    snake_id = str(position["snake_id"])

    for budget_ms in (100, 200, 300):
        results = [
            minimax_diagnostics(board, snake_id, time_budget_ms=budget_ms)
            for _ in range(3)
        ]

        # Search depth, node count, and the tied safe direction are deliberately
        # not asserted because they depend on host scheduling.
        reasons_by_completed_depth: dict[int, set[str]] = {}
        for result in results:
            assert result["move"] != "down"
            assert result["root_candidates"][result["move"]]["structural_proof"] == "safe"
            for move in ("up", "down", "left"):
                candidate = result["root_candidates"][move]
                assert candidate["allowed"] is True
                assert candidate["minimax_score"] is not None
                assert candidate["minimax_outcome"] is not None
                assert candidate["minimax_bound"] is not None
            if result["completed_depth"] > 0:
                assert result["root_comparison_reason"] != "not_compared"
                reasons_by_completed_depth.setdefault(
                    result["completed_depth"], set()
                ).add(result["root_comparison_reason"])
            _assert_selected_value_is_coherent(result)
        assert all(len(reasons) == 1 for reasons in reasons_by_completed_depth.values())


def test_root_visitation_order_changes_tags_without_changing_semantic_result() -> None:
    scenario = get_scenario("duel_late_game_long_bodies")
    results = [
        minimax_diagnostics(
            build_board(scenario),
            scenario.snake_id,
            time_budget_ms=5000,
            fixed_depth=3,
            enable_tt=False,
            enable_move_ordering=ordering,
        )
        for ordering in (False, True)
    ]

    assert {result["move"] for result in results} == {"up"}
    assert {result["root_comparison_reason"] for result in results} == {"stable_direction"}
    assert results[0]["root_candidates"]["right"]["minimax_bound"] == "upper"
    assert results[1]["root_candidates"]["right"]["minimax_bound"] == "exact"
    for result in results:
        _assert_selected_value_is_coherent(result)


def test_proved_win_precedes_structurally_safe_unresolved_root() -> None:
    board = _board(
        6,
        6,
        [(5, 1), (4, 1), (3, 1), (3, 0), (2, 0), (1, 0)],
        [(4, 0), (5, 0)],
    )

    result = minimax_diagnostics(board, "me", time_budget_ms=1000, fixed_depth=1)
    winning = result["root_candidates"]["down"]
    structurally_safe = result["root_candidates"]["up"]

    assert winning["structural_proof"] == "unknown"
    assert winning["minimax_outcome"] == "win"
    assert winning["minimax_bound"] == "exact"
    assert structurally_safe["structural_proof"] == "safe"
    assert structurally_safe["minimax_outcome"] == "unresolved"
    assert result["move"] == "down"
    assert result["root_comparison_reason"] == "terminal_outcome"
    _assert_selected_value_is_coherent(result)


def test_mixed_exact_and_upper_numeric_frontier_reports_search_bound() -> None:
    board = _board(
        7,
        7,
        [(3, 3), (3, 2), (3, 1)],
        [(6, 6), (6, 5), (6, 4)],
    )

    result = minimax_diagnostics(board, "me", time_budget_ms=1000, fixed_depth=1)

    assert result["move"] == "right"
    assert result["root_candidates"]["right"]["minimax_bound"] == "exact"
    assert result["root_candidates"]["right"]["minimax_outcome"] == "unresolved"
    assert result["root_candidates"]["left"]["minimax_bound"] == "upper"
    assert result["root_candidates"]["left"]["minimax_outcome"] == "unresolved"
    assert result["root_candidates"]["right"]["structural_proof"] == "safe"
    assert result["root_candidates"]["up"]["structural_proof"] == "safe"
    assert result["root_comparison_reason"] == "search_bound"
    _assert_selected_value_is_coherent(result)


def test_all_exact_unresolved_numeric_frontier_reports_heuristic_value() -> None:
    board = _board(
        5,
        5,
        [(0, 2), (0, 1), (0, 0)],
        [(4, 4), (4, 3), (4, 2)],
    )

    result = minimax_diagnostics(
        board,
        "me",
        time_budget_ms=1000,
        fixed_depth=1,
        enable_tt=False,
        enable_move_ordering=False,
    )
    evaluated = {
        move: candidate
        for move, candidate in result["root_candidates"].items()
        if candidate["minimax_score"] is not None
    }

    assert set(evaluated) == {"up", "right"}
    assert all(candidate["minimax_bound"] == "exact" for candidate in evaluated.values())
    assert all(candidate["minimax_outcome"] == "unresolved" for candidate in evaluated.values())
    assert {candidate["structural_proof"] for candidate in evaluated.values()} == {"safe"}
    assert evaluated["right"]["minimax_score"] > evaluated["up"]["minimax_score"]
    assert result["move"] == "right"
    assert result["root_comparison_reason"] == "heuristic_value"
    _assert_selected_value_is_coherent(result)


def test_generic_reachable_geometry_precedes_previous_pv_on_heuristic_tie() -> None:
    board = _board(
        6,
        6,
        [(1, 5), (1, 4), (2, 4), (3, 4), (3, 3)],
        [(3, 5), (2, 5)],
    )
    head = Coord(1, 5)
    left_space = reachable_space(board, Coord(head.x - 1, head.y), "me")
    right_space = reachable_space(board, Coord(head.x + 1, head.y), "me")

    result = minimax_diagnostics(
        board,
        "me",
        time_budget_ms=1000,
        fixed_depth=2,
        enable_tt=False,
        root_policy="strict_minimax",
        weights=_center_only_weights(),
    )

    assert result["root_candidates"]["left"]["minimax_score"] == result["root_candidates"]["right"]["minimax_score"]
    assert result["root_candidates"]["left"]["minimax_outcome"] == "unresolved"
    assert result["root_candidates"]["right"]["minimax_outcome"] == "unresolved"
    assert left_space > right_space
    assert result["move"] == "left"
    assert result["root_comparison_reason"] == "structural_tiebreak"
    _assert_selected_value_is_coherent(result)


def test_equal_geometry_uses_previous_pv_then_stable_direction_deterministically() -> None:
    board = _board(
        7,
        7,
        [(3, 3), (3, 2), (3, 1)],
        [(6, 6), (6, 5), (6, 4)],
    )
    zero_weights = _center_only_weights()
    zero_weights["center"] = 0.0

    first = minimax_diagnostics(
        board,
        "me",
        time_budget_ms=1000,
        fixed_depth=1,
        enable_tt=False,
        weights=zero_weights,
    )
    repeated = [
        minimax_diagnostics(
            board,
            "me",
            time_budget_ms=1000,
            fixed_depth=2,
            enable_tt=False,
            enable_move_ordering=ordering,
            weights=zero_weights,
        )
        for ordering in (False, True, False, True)
    ]

    assert first["root_comparison_reason"] == "stable_direction"
    assert {result["root_comparison_reason"] for result in repeated} == {"previous_pv"}
    assert len({_selected_tag(result) for result in repeated}) == 1


def test_low_budget_timeout_snapshot_is_coherent_smoke() -> None:
    board = _board(
        11,
        11,
        [(5, 5), (5, 4), (5, 3)],
        [(9, 9), (9, 8), (9, 7)],
    )
    # Scheduler timing is intentionally not the contract here. The C seam
    # deterministically covers both snapshot transitions; this production
    # smoke only checks per-run coherence and compares the completed snapshot
    # when this host reaches a later-depth timeout.
    timed = minimax_diagnostics(board, "me", time_budget_ms=4, enable_tt=False)
    assert timed["move"] in {"up", "down", "left", "right"}
    assert timed["max_depth_started"] >= timed["completed_depth"]
    selected = timed["root_candidates"][timed["move"]]
    if selected["minimax_score"] is not None:
        _assert_selected_value_is_coherent(timed)
    if timed["timed_out"] and timed["completed_depth"] > 0:
        complete = minimax_diagnostics(
            board,
            "me",
            time_budget_ms=5000,
            fixed_depth=timed["completed_depth"],
            enable_tt=False,
        )

        assert _selected_tag(timed) == _selected_tag(complete)
        assert timed["selection_reason"] == "timeout_best_so_far"


def test_strict_minimax_keeps_numeric_root_selection_semantics() -> None:
    position = json.loads(FIXTURE_PATH.read_text(encoding="utf-8"))["positions"][0]
    board = _board_from_fixture(position)

    strict = minimax_diagnostics(
        board,
        str(position["snake_id"]),
        time_budget_ms=5000,
        fixed_depth=1,
        root_policy="strict_minimax",
        weights=_center_only_weights(),
    )

    assert strict["move"] == "left"
    assert strict["root_candidates"]["down"]["allowed"] is True
    assert strict["root_candidates"]["down"]["minimax_score"] == 6.0
    assert strict["root_candidates"]["left"]["minimax_score"] == 6.0
    _assert_selected_value_is_coherent(strict)
