import json

import pytest

from tools import build_issue_41_fixtures


def _snake(snake_id: str, head: tuple[int, int]) -> dict[str, object]:
    return {
        "ID": snake_id,
        "Death": None,
        "Body": [{"X": head[0], "Y": head[1]}],
    }


def test_builder_defaults_are_repository_relative() -> None:
    assert build_issue_41_fixtures.EXPORT_DIR == (
        build_issue_41_fixtures.REPO_ROOT / "exports" / "zkh-dot_lost_games"
    )
    assert build_issue_41_fixtures.OUTPUT_PATH == (
        build_issue_41_fixtures.REPO_ROOT
        / "tests"
        / "fixtures"
        / "issue_41_branching_pocket_positions.json"
    )


def test_recorded_move_is_derived_from_adjacent_replay_frames() -> None:
    current = {"Turn": 41, "Snakes": [_snake("me", (3, 3))]}
    following = {"Turn": 42, "Snakes": [_snake("me", (3, 2))]}

    assert build_issue_41_fixtures._recorded_move(current, following, "me") == "down"


def test_recorded_move_rejects_nonadjacent_or_missing_frames() -> None:
    current = {"Turn": 41, "Snakes": [_snake("me", (3, 3))]}

    with pytest.raises(RuntimeError, match="adjacent frames"):
        build_issue_41_fixtures._recorded_move(
            current, {"Turn": 43, "Snakes": [_snake("me", (3, 2))]}, "me"
        )
    with pytest.raises(RuntimeError, match="missing live snake"):
        build_issue_41_fixtures._recorded_move(
            current, {"Turn": 42, "Snakes": [_snake("other", (3, 2))]}, "me"
        )


def test_builder_accepts_custom_export_directory(tmp_path, monkeypatch) -> None:
    game_id = "custom-game"
    snake = {
        "ID": "me",
        "Name": "scvnak",
        "Health": 90,
        "Death": None,
        "Body": [{"X": 1, "Y": 1}, {"X": 1, "Y": 0}],
    }
    following = {**snake, "Body": [{"X": 2, "Y": 1}, {"X": 1, "Y": 1}]}
    export = {
        "game": {"Width": 3, "Height": 3, "RulesetName": "standard"},
        "frames": [
            {"Turn": 7, "Snakes": [snake], "Food": [], "Hazards": []},
            {"Turn": 8, "Snakes": [following], "Food": [], "Hazards": []},
        ],
    }
    (tmp_path / f"{game_id}.json").write_text(json.dumps(export), encoding="utf-8")
    output = tmp_path / "fixture.json"
    monkeypatch.setattr(build_issue_41_fixtures, "POSITIONS", [(game_id, 7)])

    assert build_issue_41_fixtures.main(export_dir=tmp_path, output_path=output) == 0
    assert json.loads(output.read_text(encoding="utf-8"))["positions"][0]["evidence"][
        "recorded_bad_move"
    ] == "right"
