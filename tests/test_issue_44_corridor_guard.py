from __future__ import annotations

import json
from pathlib import Path

import pytest

from battlesnake.battlesnake_native import Board, Coord, Snake, minimax_diagnostics
from tools import build_issue_44_fixtures


FIXTURE_PATH = (
    Path(__file__).parent / "fixtures" / "issue_44_corridor_guard_positions.json"
)
POSITIONS = json.loads(FIXTURE_PATH.read_text(encoding="utf-8"))["positions"]
T424 = next(position for position in POSITIONS if position["evidence"]["turn"] == 424)
T187 = next(position for position in POSITIONS if position["evidence"]["turn"] == 187)
AUDIT_KEYS = {
    "considered",
    "incumbent",
    "proposal",
    "comparison_ordering",
    "comparison_reason",
    "exact_tie_permitted",
    "applied",
    "decision",
}
AUDIT_CANDIDATE_KEYS = {
    "move",
    "corridor_metrics",
    "structural_proof",
    "relaxed_static_capacity",
    "post_move_length",
    "minimax_score",
    "minimax_outcome",
    "minimax_bound",
}
CORRIDOR_METRIC_KEYS = {"immediate_exits", "forced_steps", "reachable"}


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


def _fixed_depth(position: dict[str, object]) -> dict[str, object]:
    return minimax_diagnostics(
        _board_from_fixture(position),
        str(position["snake_id"]),
        time_budget_ms=60_000,
        fixed_depth=11,
    )


def _snake(
    snake_id: str,
    name: str,
    body: list[tuple[int, int]],
) -> dict[str, object]:
    return {
        "ID": snake_id,
        "Name": name,
        "Health": 90,
        "Death": None,
        "Body": [{"X": x, "Y": y} for x, y in body],
    }


def test_fixture_builder_uses_live_scvnak_and_preserves_replay_evidence(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    game_id = "custom-corridor-game"
    opponent = _snake("them", "opponent", [(0, 0)])
    ours = _snake("me", "scvnak", [(1, 1), (1, 0)])
    ours_next = _snake("me", "scvnak", [(2, 1), (1, 1)])
    export = {
        "game": {
            "Width": 3,
            "Height": 3,
            "Ruleset": {"name": "standard", "hazardDamagePerTurn": 9},
        },
        "frames": [
            {
                "Turn": 7,
                "Snakes": [opponent, ours],
                "Food": [{"X": 2, "Y": 2}],
                "Hazards": [{"X": 0, "Y": 2}],
            },
            {
                "Turn": 8,
                "Snakes": [opponent, ours_next],
                "Food": [{"X": 2, "Y": 2}],
                "Hazards": [{"X": 0, "Y": 2}],
            },
        ],
    }
    (tmp_path / f"{game_id}.json").write_text(json.dumps(export), encoding="utf-8")
    output = tmp_path / "fixture.json"
    monkeypatch.setattr(
        build_issue_44_fixtures,
        "POSITIONS",
        [(game_id, 7, "right", "up")],
    )

    assert build_issue_44_fixtures.main(export_dir=tmp_path, output_path=output) == 0

    position = json.loads(output.read_text(encoding="utf-8"))["positions"][0]
    assert position["snake_id"] == "me"
    assert position["evidence"] == {
        "game_id": game_id,
        "turn": 7,
        "historical_guard_move": "right",
        "expected_authoritative_move": "up",
    }
    assert position["ruleset_name"] == "standard"
    assert position["hazard_damage"] == 9
    assert position["food"] == [[2, 2]]
    assert position["hazards"] == [[0, 2]]


def test_t424_keeps_structurally_safe_authoritative_root() -> None:
    result = _fixed_depth(T424)
    up = result["root_candidates"]["up"]
    down = result["root_candidates"]["down"]

    assert up["structural_proof"] == "safe"
    assert down["structural_proof"] == "unknown"
    assert down["relaxed_static_capacity"] < down["post_move_length"]
    assert result["move"] == "up"
    assert result["move"] != T424["evidence"]["historical_guard_move"]
    audit = result["corridor_guard"]
    assert audit["incumbent"]["move"] == "up"
    assert audit["proposal"]["move"] == "down"
    assert audit["exact_tie_permitted"] is False
    assert audit["applied"] is False
    assert audit["decision"] == "rejected_search_order"


@pytest.mark.parametrize(
    "position",
    POSITIONS,
    ids=lambda position: (
        f'{position["evidence"]["game_id"][:8]}-t{position["evidence"]["turn"]}'
    ),
)
def test_corridor_guard_audit_is_complete_and_coherent(
    position: dict[str, object],
) -> None:
    result = _fixed_depth(position)
    audit = result["corridor_guard"]

    assert set(audit) == AUDIT_KEYS
    assert isinstance(audit["considered"], bool)
    assert isinstance(audit["exact_tie_permitted"], bool)
    assert isinstance(audit["applied"], bool)
    assert isinstance(audit["comparison_ordering"], str)
    assert audit["comparison_ordering"]
    assert isinstance(audit["comparison_reason"], str)
    assert audit["comparison_reason"]
    assert isinstance(audit["decision"], str)
    assert audit["decision"]

    for candidate_name in ("incumbent", "proposal"):
        candidate = audit[candidate_name]
        assert set(candidate) == AUDIT_CANDIDATE_KEYS
        assert set(candidate["corridor_metrics"]) == CORRIDOR_METRIC_KEYS
        root = result["root_candidates"][candidate["move"]]
        for key in (
            "structural_proof",
            "relaxed_static_capacity",
            "post_move_length",
            "minimax_score",
            "minimax_outcome",
            "minimax_bound",
        ):
            assert candidate[key] == root[key]

    assert audit["proposal"]["move"] == position["evidence"]["historical_guard_move"]
    if audit["proposal"]["move"] != audit["incumbent"]["move"]:
        assert not audit["applied"] or audit["exact_tie_permitted"]
    if audit["applied"]:
        assert result["move"] == audit["proposal"]["move"]
    else:
        assert result["move"] == audit["incumbent"]["move"]

    selected = result["root_candidates"][result["move"]]
    assert result["score"] == selected["minimax_score"]
    assert result["root_move_scores"][result["move"]] == result["score"]


def test_t187_rejects_equal_numeric_but_structurally_different_proposal() -> None:
    result = _fixed_depth(T187)
    audit = result["corridor_guard"]

    assert audit["incumbent"]["move"] == "down"
    assert audit["proposal"]["move"] == "right"
    assert audit["incumbent"]["minimax_score"] == audit["proposal"]["minimax_score"]
    assert audit["considered"] is True
    assert audit["exact_tie_permitted"] is False
    assert audit["applied"] is False
    assert audit["decision"] == "rejected_search_order"
    assert result["move"] == "down"
