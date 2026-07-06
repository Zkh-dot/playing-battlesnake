from __future__ import annotations

from typing import Any

import numpy as np
import pandas as pd


def _metric_float(value: float) -> float:
    return float(round(value, 12))


def _metrics_for_frame(frame: pd.DataFrame, score_column: str) -> dict[str, Any]:
    observations = int(frame["observation_id"].nunique())
    if observations == 0:
        return {
            "observations": 0,
            "top1_correct": 0,
            "top1_accuracy": 0.0,
            "actual_move_mean_score": 0.0,
            "actual_move_mean_probability": 0.0,
            "group_negative_log_loss": 0.0,
        }

    best = frame.sort_values(score_column, ascending=False).groupby("observation_id", as_index=False).head(1)
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
    actual_score = _metric_float(float(actual[score_column].mean())) if len(actual) else 0.0
    actual_probability = _metric_float(float(actual["_normalized_score"].mean())) if len(actual) else 0.0
    group_nll = _metric_float(float(-np.log(actual["_normalized_score"].clip(lower=1e-12)).mean())) if len(actual) else 0.0
    return {
        "observations": observations,
        "top1_correct": len(correct_ids),
        "top1_accuracy": _metric_float(len(correct_ids) / observations),
        "actual_move_mean_score": actual_score,
        "actual_move_mean_probability": actual_probability,
        "group_negative_log_loss": group_nll,
    }


def grouped_move_metrics(frame: pd.DataFrame, *, score_column: str) -> dict[str, Any]:
    metrics = _metrics_for_frame(frame, score_column)
    per_snake: dict[str, Any] = {}
    for snake_name, group in frame.groupby("snake_name"):
        per_snake[str(snake_name)] = _metrics_for_frame(group, score_column)
    metrics["per_snake"] = per_snake
    return metrics
