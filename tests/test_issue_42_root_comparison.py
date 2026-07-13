from __future__ import annotations

import json
from pathlib import Path

from battlesnake.battlesnake_native import Board, Coord, Snake, minimax_diagnostics, reachable_space


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
    assert result["root_comparison_reason"] == "structural_proof"
    _assert_selected_value_is_coherent(result)


def test_t169_default_weights_report_structural_frontier_and_ignore_move_order() -> None:
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

    assert {result["root_comparison_reason"] for result in results} == {"structural_proof"}
    assert len({_selected_tag(result) for result in results}) == 1


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


def test_exact_unresolved_heuristic_value_is_reported() -> None:
    board = _board(
        7,
        7,
        [(3, 3), (3, 2), (3, 1)],
        [(6, 6), (6, 5), (6, 4)],
    )

    result = minimax_diagnostics(board, "me", time_budget_ms=1000, fixed_depth=1)

    assert result["move"] == "right"
    assert result["root_candidates"]["right"]["minimax_bound"] == "exact"
    assert result["root_candidates"]["right"]["structural_proof"] == "safe"
    assert result["root_candidates"]["up"]["structural_proof"] == "safe"
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


def test_later_timeout_preserves_last_complete_value_and_reason() -> None:
    board = _board(
        11,
        11,
        [(5, 5), (5, 4), (5, 3)],
        [(9, 9), (9, 8), (9, 7)],
    )
    for _ in range(20):
        timed = None
        for budget_ms in (2, 4, 8, 16, 32):
            candidate = minimax_diagnostics(board, "me", time_budget_ms=budget_ms, enable_tt=False)
            if candidate["timed_out"] and candidate["completed_depth"] > 0:
                timed = candidate
                break
        assert timed is not None
        assert timed["max_depth_started"] > timed["completed_depth"]

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
