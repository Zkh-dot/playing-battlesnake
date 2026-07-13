import pytest

from tools import build_issue_41_fixtures


def _snake(snake_id: str, head: tuple[int, int]) -> dict[str, object]:
    return {
        "ID": snake_id,
        "Death": None,
        "Body": [{"X": head[0], "Y": head[1]}],
    }


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
