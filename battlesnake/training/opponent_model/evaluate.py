from __future__ import annotations

from typing import Any

import numpy as np
import pandas as pd

from battlesnake.training.opponent_model.schema import MOVES


_MOVE_ORDER = {move: index for index, move in enumerate(MOVES)}


def _validate_observation_groups(frame: pd.DataFrame) -> None:
    for observation_id, group in frame.groupby("observation_id", sort=False):
        if len(group) != len(MOVES):
            raise ValueError(f"observation_id {observation_id!r} has {len(group)} candidates; expected {len(MOVES)}")

        positives = int((group["label"].astype(int) == 1).sum())
        if positives != 1:
            raise ValueError(f"observation_id {observation_id!r} has {positives} positive labels; expected 1")

        duplicate_moves = group["candidate_move"][group["candidate_move"].duplicated()].unique()
        if len(duplicate_moves):
            moves = ", ".join(str(move) for move in duplicate_moves)
            raise ValueError(f"observation_id {observation_id!r} has duplicate candidate moves: {moves}")


def _metrics_for_frame(frame: pd.DataFrame, score_column: str) -> dict[str, Any]:
    if len(frame) == 0:
        return {
            "observations": 0,
            "top1_correct": 0,
            "top1_accuracy": 0.0,
            "actual_move_mean_score": 0.0,
            "actual_move_mean_probability": 0.0,
            "group_negative_log_loss": 0.0,
        }

    observations = int(frame["observation_id"].nunique())
    _validate_observation_groups(frame)

    ranked = frame.assign(_move_order=frame["candidate_move"].map(_MOVE_ORDER).fillna(len(MOVES)).astype(int))
    best = ranked.sort_values([score_column, "_move_order"], ascending=[False, True], kind="mergesort").groupby(
        "observation_id", as_index=False
    ).head(1)
    clipped_scores = frame[score_column].astype(float).clip(lower=0.0)
    score_sums = clipped_scores.groupby(frame["observation_id"]).transform("sum")
    candidates_per_observation = frame.groupby("observation_id")[score_column].transform("size")
    normalized_scores = np.where(
        score_sums.gt(0.0),
        clipped_scores / score_sums,
        1.0 / candidates_per_observation,
    )
    scored = frame.assign(_normalized_score=normalized_scores)
    actual = scored[scored["label"].astype(int) == 1]
    correct_ids = set(best[best["label"].astype(int) == 1]["observation_id"])
    actual_score = float(actual[score_column].mean()) if len(actual) else 0.0
    actual_probability = float(actual["_normalized_score"].mean()) if len(actual) else 0.0
    group_nll = float(-np.log(actual["_normalized_score"].clip(lower=1e-12)).mean()) if len(actual) else 0.0
    return {
        "observations": observations,
        "top1_correct": len(correct_ids),
        "top1_accuracy": len(correct_ids) / observations,
        "actual_move_mean_score": actual_score,
        "actual_move_mean_probability": actual_probability,
        "group_negative_log_loss": group_nll,
    }


def grouped_move_metrics(frame: pd.DataFrame, *, score_column: str) -> dict[str, Any]:
    metrics = _metrics_for_frame(frame, score_column)
    if len(frame) == 0:
        metrics["per_snake"] = {}
        return metrics

    snake_column = "snake_id" if "snake_id" in frame.columns else "snake_name"
    per_snake: dict[str, Any] = {}
    for snake_key, group in frame.groupby(snake_column):
        per_snake[str(snake_key)] = _metrics_for_frame(group, score_column)
    metrics["per_snake"] = per_snake
    return metrics
