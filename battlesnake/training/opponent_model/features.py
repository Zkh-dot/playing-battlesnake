from __future__ import annotations

from collections.abc import Iterator

from battlesnake.battlesnake_native import Board
from battlesnake.opponent_model_features import candidate_feature_rows
from battlesnake.training.opponent_model.schema import CandidateRow, MoveObservation


def candidate_rows(observation: MoveObservation, board: Board) -> Iterator[CandidateRow]:
    runtime_rows = candidate_feature_rows(
        board=board,
        snake_id=observation.snake_id,
        turn=observation.turn,
        snake_rank=observation.snake_rank,
        alive_snakes=observation.alive_snakes,
    )
    for row in runtime_rows:
        yield CandidateRow(
            observation_id=observation.observation_id,
            game_id=observation.game_id,
            split=observation.split,
            turn=observation.turn,
            snake_id=observation.snake_id,
            snake_name=observation.snake_name,
            snake_rank=observation.snake_rank,
            candidate_move=row.candidate_move,
            label=1 if row.candidate_move == observation.target_move else 0,
            features=dict(row.features),
        )
