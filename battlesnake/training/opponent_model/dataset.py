from __future__ import annotations

import csv
from collections import Counter
from pathlib import Path
from typing import Iterable

from battlesnake.training.opponent_model.schema import CandidateRow

BASE_COLUMNS = [
    "observation_id",
    "game_id",
    "split",
    "turn",
    "snake_id",
    "snake_name",
    "snake_rank",
    "candidate_move",
    "label",
]
BASE_COLUMN_SET = set(BASE_COLUMNS)


def _feature_column_name(name: str) -> str:
    if name in BASE_COLUMN_SET:
        return f"feature_{name}"
    return name


def _feature_columns(feature_names: Iterable[str]) -> list[tuple[str, str]]:
    columns = [(name, _feature_column_name(name)) for name in set(feature_names)]
    columns.sort(key=lambda item: item[1])

    seen: set[str] = set()
    for _, column_name in columns:
        if column_name in seen:
            raise ValueError(f"Feature column collision after namespacing: {column_name}")
        seen.add(column_name)

    return columns


def flatten_row(row: CandidateRow, feature_columns: list[tuple[str, str]]) -> dict[str, object]:
    data: dict[str, object] = {
        "observation_id": row.observation_id,
        "game_id": row.game_id,
        "split": row.split,
        "turn": row.turn,
        "snake_id": row.snake_id,
        "snake_name": row.snake_name,
        "snake_rank": row.snake_rank if row.snake_rank is not None else "",
        "candidate_move": row.candidate_move,
        "label": row.label,
    }
    for name, column_name in feature_columns:
        data[column_name] = row.features.get(name, "")
    return data


def write_candidate_rows(rows: Iterable[CandidateRow], output_path: Path) -> dict[str, object]:
    materialized = list(rows)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    feature_columns = _feature_columns(key for row in materialized for key in row.features)
    fieldnames = BASE_COLUMNS + [column_name for _, column_name in feature_columns]
    with output_path.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in materialized:
            writer.writerow(flatten_row(row, feature_columns))

    split_counts = Counter(row.split for row in materialized)
    return {
        "rows": len(materialized),
        "observations": len({row.observation_id for row in materialized}),
        "games": len({row.game_id for row in materialized}),
        "snakes": len({row.snake_id for row in materialized}),
        "split_rows": dict(sorted(split_counts.items())),
        "output_path": str(output_path),
    }
