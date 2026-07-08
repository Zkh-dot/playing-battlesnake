from __future__ import annotations

from pathlib import Path

import pytest

from battlesnake.battlesnake_native import Board, Coord, Snake
from battlesnake.opponent_model_features import candidate_feature_rows
from battlesnake.opponent_model_prior import LightGBMOpponentPrior, uniform_safe_priors
from battlesnake.strategies.standard import StrategyStandard
from battlesnake.training.opponent_model.features import candidate_rows
from battlesnake.training.opponent_model.schema import MoveObservation


def c(x: int, y: int) -> Coord:
    return Coord(x, y)


def snake(snake_id: str, body: list[tuple[int, int]], *, health: int = 100) -> Snake:
    return Snake(id=snake_id, name=snake_id, health=health, body=[c(x, y) for x, y in body])


def sample_board() -> Board:
    return Board(
        7,
        7,
        [
            snake("me", [(2, 2), (2, 1), (2, 0)]),
            snake("opp", [(4, 2), (4, 1), (4, 0)]),
            snake("wall", [(6, 6), (6, 5), (6, 4)]),
        ],
        food=[c(3, 2), c(5, 5)],
    )


def observation() -> MoveObservation:
    return MoveObservation(
        observation_id="g:12:opp",
        game_id="g",
        split="test",
        turn=12,
        snake_id="opp",
        snake_name="Opponent",
        snake_rank=2,
        target_move="left",
        board_width=7,
        board_height=7,
        alive_snakes=3,
    )


def test_runtime_feature_rows_match_training_candidate_rows() -> None:
    board = sample_board()
    obs = observation()

    runtime = {row.candidate_move: row.features for row in candidate_feature_rows(
        board=board,
        snake_id=obs.snake_id,
        turn=obs.turn,
        snake_rank=obs.snake_rank,
        alive_snakes=obs.alive_snakes,
    )}
    training = {row.candidate_move: row.features for row in candidate_rows(obs, board)}

    assert runtime == training


def test_model_prior_missing_artifact_falls_back_to_uniform_safe() -> None:
    board = sample_board()
    prior = LightGBMOpponentPrior(model_path=Path("/tmp/does-not-exist.joblib.gz"), timeout_ms=80)

    assert prior.priors(board, "me", turn=12) == uniform_safe_priors(board, "me")
    assert prior.last_status == "error_fallback"


def test_model_prior_smooths_and_keeps_only_safe_moves(monkeypatch: pytest.MonkeyPatch) -> None:
    board = sample_board()
    prior = LightGBMOpponentPrior(model=object(), epsilon=0.10, timeout_ms=80)

    def fake_positive_probabilities(_model: object, rows: list[dict[str, object]]) -> dict[str, float]:
        return {str(row["feature_candidate_move"]): 10.0 if row["feature_candidate_move"] == "up" else 1.0 for row in rows}

    monkeypatch.setattr("battlesnake.opponent_model_prior._positive_probabilities", fake_positive_probabilities)

    priors = prior.priors(board, "me", turn=12)

    for opponent_id, moves in priors.items():
        safe = set(board.safe_moves(opponent_id)) or {"up", "down", "left", "right"}
        assert {move for move, _probability in moves} <= safe
        assert sum(probability for _move, probability in moves) == pytest.approx(1.0)
    assert priors["opp"][0][0] == "up"
    assert prior.last_status == "model"


def test_strategy_model_prior_is_only_a_prior_and_hard_gates_still_apply(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    board = Board(
        7,
        7,
        [
            snake("me", [(2, 2), (2, 1), (2, 0)]),
            snake("equal", [(4, 2), (4, 1), (4, 0)]),
            snake("far", [(6, 6)]),
        ],
    )

    monkeypatch.setattr(
        "battlesnake.opponent_model_prior.LightGBMOpponentPrior.priors",
        lambda self, board, snake_id, turn=0: uniform_safe_priors(board, snake_id),
    )

    strategy = StrategyStandard(opponent_prior="model")
    assert strategy.decide(board, "me") != "right"


def test_standard_v1_dev_variant_uses_model_prior() -> None:
    from battlesnake.main import STANDARD_VARIANTS

    assert "standard-v1-model-prior" not in STANDARD_VARIANTS
    strategy = STANDARD_VARIANTS["standard-v1"]()
    assert isinstance(strategy, StrategyStandard)
    assert strategy.opponent_prior == "model"


def test_standard_v1_decision_record_uses_model_prior_when_available(monkeypatch: pytest.MonkeyPatch) -> None:
    from battlesnake.main import STANDARD_VARIANTS

    monkeypatch.setattr("battlesnake.opponent_model_prior._load_model", lambda _path: object())

    def fake_positive_probabilities(_model: object, rows: list[dict[str, object]]) -> dict[str, float]:
        return {str(row["feature_candidate_move"]): 10.0 if row["feature_candidate_move"] == "up" else 1.0 for row in rows}

    monkeypatch.setattr("battlesnake.opponent_model_prior._positive_probabilities", fake_positive_probabilities)

    strategy = STANDARD_VARIANTS["standard-v1"]()
    _move, record = strategy.explain_decision(sample_board(), "me")

    assert record["opponent_prior_status"]["status"] == "model"
    assert record["opponent_prior_status"]["source"] == "model"
    assert any(
        len({round(item["probability"], 6) for item in moves}) > 1
        for moves in record["opponent_priors"].values()
    )
