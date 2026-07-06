from __future__ import annotations

from typing import Any

import pandas as pd


def dataset_summary(frame: pd.DataFrame) -> dict[str, Any]:
    positives = frame[frame["label"].astype(int) == 1]
    by_snake = positives.groupby("snake_name")["observation_id"].nunique().sort_values(ascending=False)
    return {
        "rows": int(len(frame)),
        "observations": int(frame["observation_id"].nunique()),
        "games": int(frame["game_id"].nunique()),
        "snakes": int(frame["snake_name"].nunique()),
        "splits": {str(key): int(value) for key, value in frame["split"].value_counts().sort_index().items()},
        "target_moves": {str(key): int(value) for key, value in positives["candidate_move"].value_counts().sort_index().items()},
        "top_snakes_by_observations": {str(key): int(value) for key, value in by_snake.head(20).items()},
    }
