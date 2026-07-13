from __future__ import annotations

import json
from pathlib import Path

import pytest

from battlesnake.battlesnake_native import Board, Coord, Snake, duel_root_profile, minimax_diagnostics
from battlesnake.main import fallback_move
from battlesnake.strategies.duel import StrategyDuel
from battlesnake.strategies.first_safe import StrategyFirstSafe
from battlesnake.strategies.standard import StrategyStandard
from battlesnake.types import Move


FIXTURE_PATH = Path(__file__).parent / "fixtures" / "issue_38_dead_tunnel_positions.json"


def _positions() -> list[dict[str, object]]:
    return json.loads(FIXTURE_PATH.read_text(encoding="utf-8"))["positions"]


def _board(raw: dict[str, object]) -> Board:
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


@pytest.mark.parametrize("budget_ms", [50, 100, 150, 200, 300])
def test_t209_opportunity_policy_rejects_proven_short_self_trap(budget_ms: int) -> None:
    raw = _positions()[0]
    result = minimax_diagnostics(_board(raw), str(raw["snake_id"]), time_budget_ms=budget_ms)
    trapped = result["root_candidates"]["up"]
    alternative = result["root_candidates"]["down"]

    assert result["move"] == "down"
    assert result["root_policy_applied"] == "standard_ladder_opportunity"
    assert trapped["allowed"] is False
    assert trapped["rejection_reason"] == "proven_short_self_trap"
    assert trapped["trap_status"] == "proven_self_trap"
    assert trapped["trap_horizon"] == 11
    assert trapped["post_move_length"] == 24
    assert trapped["relaxed_static_capacity"] == 11
    assert trapped["refutation_status"] == "proven_refutable"
    assert alternative["allowed"] is True
    assert alternative["trap_status"] == "open_branch"
    assert alternative["relaxed_static_capacity"] >= alternative["post_move_length"]


def test_t209_strict_minimax_keeps_adversarially_safer_move() -> None:
    raw = _positions()[0]
    result = minimax_diagnostics(
        _board(raw),
        str(raw["snake_id"]),
        time_budget_ms=5000,
        fixed_depth=6,
        root_policy="strict_minimax",
    )

    assert result["move"] == "up"
    assert result["root_policy_applied"] == "strict_minimax"
    assert result["root_candidates"]["up"]["trap_status"] == "proven_self_trap"
    assert result["root_candidates"]["up"]["allowed"] is True


def test_t209_down_is_an_intentional_risk_with_a_short_adversarial_loss_signal() -> None:
    raw = _positions()[0]
    result = minimax_diagnostics(
        _board(raw),
        str(raw["snake_id"]),
        time_budget_ms=5000,
        fixed_depth=3,
    )
    selected = result["root_candidates"]["down"]

    assert result["move"] == "down"
    assert selected["minimax_score"] == -1_000_000.0
    assert selected["minimax_outcome"] == "unresolved"


@pytest.mark.parametrize("budget_ms", [50, 100, 150, 200, 300])
def test_t586_rule_a_prefers_contingent_survival(budget_ms: int) -> None:
    raw = _positions()[1]
    result = minimax_diagnostics(_board(raw), str(raw["snake_id"]), time_budget_ms=budget_ms)
    down = result["root_candidates"]["down"]
    right = result["root_candidates"]["right"]

    assert result["move"] == "right"
    assert down["alive_reply_count"] == 0
    assert down["trap_status"] == "immediate_death"
    assert "self_body" in down["immediate_causes"]
    assert down["rejection_reason"] == "no_surviving_reply"
    assert right["alive_reply_count"] == 2
    assert right["allowed"] is True


def test_t586_native_profile_and_python_fallbacks_agree() -> None:
    raw = _positions()[1]
    board = _board(raw)
    snake_id = str(raw["snake_id"])
    profile = duel_root_profile(board, snake_id)

    assert board.safe_moves(snake_id) == []
    assert profile["down"]["alive_reply_count"] == 0
    assert profile["right"]["alive_reply_count"] == 2
    assert StrategyFirstSafe().decide(board, snake_id) is Move.RIGHT
    assert StrategyDuel._fallback_move(board, snake_id) is Move.RIGHT
    assert fallback_move(board, snake_id) is Move.RIGHT


def _issue_39_board() -> Board:
    return Board(
        width=11,
        height=11,
        snakes={
            "me": Snake("me", "me", 96, [Coord(6, 10), Coord(7, 10), Coord(7, 9), Coord(6, 9)]),
            "test": Snake("test", "test", 96, [Coord(5, 9), Coord(5, 8), Coord(5, 7), Coord(5, 6)]),
        },
        food=[Coord(0, 4), Coord(10, 4), Coord(4, 0)],
        ruleset_name="standard",
        hazard_damage=0,
    )


def test_issue_39_no_safe_fallback_never_chooses_wall_or_self_collision() -> None:
    board = _issue_39_board()
    profile = duel_root_profile(board, "me")

    assert board.safe_moves("me") == []
    assert profile["up"]["alive_reply_count"] == 0
    assert "wall" in profile["up"]["immediate_causes"]
    assert profile["right"]["alive_reply_count"] == 0
    assert "self_body" in profile["right"]["immediate_causes"]
    assert StrategyFirstSafe().decide(board, "me") in {Move.DOWN, Move.LEFT}
    assert StrategyStandard()._fallback.decide(board, "me") in {Move.DOWN, Move.LEFT}
    assert fallback_move(board, "me") in {Move.DOWN, Move.LEFT}


def test_profile_reports_head_to_head_draw_loss_and_mutual_wall_death() -> None:
    equal = Board(
        width=5,
        height=5,
        snakes={
            "me": Snake("me", "me", 90, [Coord(1, 2), Coord(1, 1), Coord(1, 0)]),
            "you": Snake("you", "you", 90, [Coord(3, 2), Coord(3, 1), Coord(3, 0)]),
        },
        ruleset_name="standard",
        hazard_damage=0,
    )
    equal_profile = duel_root_profile(equal, "me")
    assert equal_profile["right"]["reply_outcomes"]["left"] == "draw"

    longer = Board(
        width=5,
        height=5,
        snakes={
            "me": Snake("me", "me", 90, [Coord(1, 2), Coord(1, 1), Coord(1, 0)]),
            "you": Snake("you", "you", 90, [Coord(3, 2), Coord(3, 1), Coord(3, 0), Coord(4, 0)]),
        },
        ruleset_name="standard",
        hazard_damage=0,
    )
    assert duel_root_profile(longer, "me")["right"]["reply_outcomes"]["left"] == "loss"

    walls = Board(
        width=3,
        height=3,
        snakes={
            "me": Snake("me", "me", 90, [Coord(0, 2), Coord(0, 1), Coord(0, 0)]),
            "you": Snake("you", "you", 90, [Coord(2, 2), Coord(2, 1), Coord(2, 0)]),
        },
        ruleset_name="standard",
        hazard_damage=0,
    )
    assert duel_root_profile(walls, "me")["up"]["reply_outcomes"]["up"] == "draw"


def test_structural_dominance_can_exclude_draw_when_safe_win_exists() -> None:
    board = Board(
        width=5,
        height=5,
        snakes={
            "me": Snake("me", "me", 90, [Coord(0, 4), Coord(0, 3), Coord(0, 2)]),
            "you": Snake(
                "you",
                "you",
                90,
                [Coord(4, 4), Coord(4, 3), Coord(3, 3), Coord(3, 4), Coord(2, 4)],
            ),
        },
        ruleset_name="standard",
        hazard_damage=0,
    )
    result = minimax_diagnostics(board, "me", time_budget_ms=1000, fixed_depth=1)
    guaranteed_draw = result["root_candidates"]["up"]

    assert set(guaranteed_draw["reply_outcomes"].values()) == {"draw"}
    assert guaranteed_draw["alive_reply_count"] == 0
    assert guaranteed_draw["allowed"] is False
    assert guaranteed_draw["rejection_reason"] == "structurally_dominated"
    assert guaranteed_draw["minimax_outcome"] is None


def test_profile_distinguishes_starvation_hazard_and_length_two_reverse() -> None:
    starvation = Board(
        width=7,
        height=7,
        snakes={
            "me": Snake("me", "me", 1, [Coord(1, 1), Coord(1, 0)]),
            "you": Snake("you", "you", 90, [Coord(5, 5), Coord(4, 5)]),
        },
        ruleset_name="standard",
        hazard_damage=20,
    )
    assert "starvation" in duel_root_profile(starvation, "me")["up"]["immediate_causes"]

    hazard = Board(
        width=7,
        height=7,
        snakes={
            "me": Snake("me", "me", 20, [Coord(1, 1), Coord(1, 0)]),
            "you": Snake("you", "you", 90, [Coord(5, 5), Coord(4, 5)]),
        },
        hazards=[Coord(1, 2)],
        ruleset_name="standard",
        hazard_damage=20,
    )
    profile = duel_root_profile(hazard, "me")
    assert "hazard" in profile["up"]["immediate_causes"]
    assert "left" in profile["up"]["reply_outcomes"]


def test_root_food_growth_is_reflected_in_post_move_length() -> None:
    def diagnostics(with_food: bool) -> dict[str, object]:
        board = Board(
            width=7,
            height=7,
            snakes={
                "me": Snake("me", "me", 90, [Coord(1, 2), Coord(1, 1), Coord(1, 0)]),
                "you": Snake("you", "you", 90, [Coord(5, 5), Coord(5, 4), Coord(5, 3)]),
            },
            food=[Coord(1, 3)] if with_food else [],
            ruleset_name="standard",
            hazard_damage=0,
        )
        return minimax_diagnostics(board, "me", time_budget_ms=1000, fixed_depth=1)

    assert diagnostics(True)["root_candidates"]["up"]["post_move_length"] == 4
    assert diagnostics(False)["root_candidates"]["up"]["post_move_length"] == 3


def test_profile_rejects_non_duel_boards() -> None:
    board = Board(
        width=5,
        height=5,
        snakes={"me": Snake("me", "me", 90, [Coord(2, 2), Coord(2, 1), Coord(2, 0)])},
        ruleset_name="standard",
        hazard_damage=0,
    )
    with pytest.raises(RuntimeError):
        duel_root_profile(board, "me")
