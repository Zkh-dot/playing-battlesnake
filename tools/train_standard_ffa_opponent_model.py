#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from importlib import metadata
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[1]
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

import pandas as pd

from battlesnake.training.opponent_model.archive_loader import (
    iter_replay_exports,
    load_manifest,
    player_rank_by_display,
)
from battlesnake.training.opponent_model.dataset import write_candidate_rows_streaming
from battlesnake.training.opponent_model.evaluate import grouped_move_metrics
from battlesnake.training.opponent_model.features import candidate_rows
from battlesnake.training.opponent_model.models import (
    build_gbdt_pipeline,
    build_logistic_pipeline,
    build_move_prior_model,
    fit_model,
    save_model,
    score_frame,
)
from battlesnake.training.opponent_model.replay_reader import iter_move_observations


DEFAULT_ARCHIVE = Path("exports/battlesnake_top150_games_gt50.zip")
DEFAULT_OUT_DIR = Path("ai-artifacts/opponent-model")
DEFAULT_MAX_TRAIN_OBSERVATIONS = 200_000
DEFAULT_CSV_CHUNKSIZE = 250_000
SEED = 17


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _git_output(*args: str) -> str:
    try:
        return subprocess.check_output(["git", *args], stderr=subprocess.DEVNULL, text=True).strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def _package_versions() -> dict[str, str]:
    versions = {}
    for package in ["numpy", "pandas", "scikit-learn", "joblib"]:
        try:
            versions[package] = metadata.version(package)
        except metadata.PackageNotFoundError:
            versions[package] = "not-installed"
    return versions


def write_run_metadata(archive: Path, out_dir: Path) -> str:
    git_status = _git_output("status", "--short")
    payload = {
        "archive": str(archive),
        "archive_sha256": _sha256(archive),
        "git_sha": _git_output("rev-parse", "HEAD"),
        "git_dirty": git_status not in {"", "unknown"},
        "python": sys.version,
        "packages": _package_versions(),
        "seed": SEED,
    }
    path = out_dir / "run_metadata.json"
    path.write_text(json.dumps(payload, indent=2, sort_keys=True))
    return str(path)


def _candidate_row_iter(archive: Path):
    manifest = load_manifest(archive)
    ranks_by_display = player_rank_by_display(manifest)
    for archive_name, export in iter_replay_exports(archive):
        for board_observation in iter_move_observations(archive_name, export, ranks_by_display):
            yield from candidate_rows(board_observation.observation, board_observation.board)


def _summarize_dataset_csv(dataset_path: Path, chunksize: int) -> dict[str, object]:
    rows = 0
    observation_ids: set[str] = set()
    game_ids: set[str] = set()
    snake_ids: set[str] = set()
    split_counts: dict[str, int] = {}
    target_move_counts: dict[str, int] = {}
    snake_observation_counts: dict[str, int] = {}

    for chunk in pd.read_csv(dataset_path, chunksize=chunksize):
        rows += int(len(chunk))
        observation_ids.update(chunk["observation_id"].astype(str))
        game_ids.update(chunk["game_id"].astype(str))
        snake_ids.update(chunk["snake_id"].astype(str))
        for split, count in chunk["split"].value_counts().items():
            split_counts[str(split)] = split_counts.get(str(split), 0) + int(count)

        positives = chunk[chunk["label"].astype(int) == 1]
        for move, count in positives["candidate_move"].value_counts().items():
            target_move_counts[str(move)] = target_move_counts.get(str(move), 0) + int(count)
        for snake_id, count in positives.groupby("snake_id")["observation_id"].nunique().items():
            snake_key = str(snake_id)
            snake_observation_counts[snake_key] = snake_observation_counts.get(snake_key, 0) + int(count)

    top_snakes = dict(sorted(snake_observation_counts.items(), key=lambda item: item[1], reverse=True)[:20])
    return {
        "rows": rows,
        "observations": len(observation_ids),
        "games": len(game_ids),
        "snakes": len(snake_ids),
        "splits": dict(sorted(split_counts.items())),
        "target_moves": dict(sorted(target_move_counts.items())),
        "top_snakes_by_observations": top_snakes,
    }


def build_dataset(archive: Path, out_dir: Path, *, chunksize: int) -> dict[str, object]:
    dataset_path = out_dir / "candidate_rows.csv"
    writer_summary = write_candidate_rows_streaming(_candidate_row_iter(archive), dataset_path)

    summary_path = out_dir / "dataset_summary.json"
    summary_path.write_text(json.dumps(_summarize_dataset_csv(dataset_path, chunksize), indent=2, sort_keys=True))

    return {
        "dataset_csv": str(dataset_path),
        "summary_json": str(summary_path),
        **writer_summary,
    }


def _model_builders() -> dict[str, Any]:
    return {
        "move_prior": build_move_prior_model,
        "logistic": build_logistic_pipeline,
        "gbdt": build_gbdt_pipeline,
    }


def _limit_observations(frame: pd.DataFrame, max_observations: int | None) -> pd.DataFrame:
    if max_observations is None or max_observations <= 0:
        return frame
    observation_ids = frame["observation_id"].drop_duplicates().head(max_observations)
    return frame[frame["observation_id"].isin(observation_ids)].copy()


def _read_split_frame(
    dataset_csv: Path,
    split: str,
    *,
    chunksize: int,
    max_observations: int | None = None,
) -> pd.DataFrame:
    frames: list[pd.DataFrame] = []
    remaining_observations = max_observations
    for chunk in pd.read_csv(dataset_csv, chunksize=chunksize):
        split_frame = chunk[chunk["split"] == split].copy()
        if split_frame.empty:
            continue
        if remaining_observations is not None and remaining_observations > 0:
            split_frame = _limit_observations(split_frame, remaining_observations)
            remaining_observations -= int(split_frame["observation_id"].nunique())
        frames.append(split_frame)
        if remaining_observations is not None and remaining_observations <= 0:
            break
    if not frames:
        return pd.DataFrame()
    return pd.concat(frames, ignore_index=True)


def train_and_evaluate(
    dataset_csv: Path,
    out_dir: Path,
    *,
    max_train_observations: int,
    chunksize: int,
) -> dict[str, object]:
    train = _read_split_frame(
        dataset_csv,
        "train",
        chunksize=chunksize,
        max_observations=max_train_observations,
    )
    validation = _read_split_frame(dataset_csv, "validation", chunksize=chunksize)
    test = _read_split_frame(dataset_csv, "test", chunksize=chunksize)

    if train.empty:
        raise ValueError("Training split is empty; cannot train opponent models")
    if validation.empty:
        raise ValueError("Validation split is empty; cannot select the best opponent model")

    results: dict[str, dict[str, object]] = {}
    fitted_models: dict[str, Any] = {}
    for name, builder in _model_builders().items():
        model = fit_model(builder(), train)
        fitted_models[name] = model
        scored_validation = score_frame(model, validation, score_column="score")
        results[name] = {
            "validation": grouped_move_metrics(scored_validation, score_column="score"),
        }

    best_name = max(
        results,
        key=lambda name: float(results[name]["validation"]["top1_accuracy"]),  # type: ignore[index]
    )
    if test.empty:
        results[best_name]["test"] = grouped_move_metrics(test, score_column="score")
    else:
        scored_test = score_frame(fitted_models[best_name], test, score_column="score")
        results[best_name]["test"] = grouped_move_metrics(scored_test, score_column="score")

    metrics_path = out_dir / "metrics.json"
    model_path = out_dir / f"{best_name}.joblib"
    metrics_path.write_text(
        json.dumps(
            {
                "best_model": best_name,
                "models": results,
                "training_rows": int(len(train)),
                "training_observations": int(train["observation_id"].nunique()),
                "max_train_observations": max_train_observations,
            },
            indent=2,
            sort_keys=True,
        )
    )
    save_model(fitted_models[best_name], model_path)
    return {
        "best_model": best_name,
        "metrics_json": str(metrics_path),
        "model_path": str(model_path),
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Train a standard FFA Battlesnake opponent model.")
    parser.add_argument("--archive", type=Path, default=DEFAULT_ARCHIVE)
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUT_DIR)
    parser.add_argument("--dataset-only", action="store_true")
    parser.add_argument("--max-train-observations", type=int, default=DEFAULT_MAX_TRAIN_OBSERVATIONS)
    parser.add_argument("--csv-chunksize", type=int, default=DEFAULT_CSV_CHUNKSIZE)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    args.out_dir.mkdir(parents=True, exist_ok=True)

    dataset_result = build_dataset(args.archive, args.out_dir, chunksize=args.csv_chunksize)
    result = {
        "run_metadata_json": write_run_metadata(args.archive, args.out_dir),
        **dataset_result,
    }
    if not args.dataset_only:
        result.update(
            train_and_evaluate(
                Path(dataset_result["dataset_csv"]),
                args.out_dir,
                max_train_observations=args.max_train_observations,
                chunksize=args.csv_chunksize,
            )
        )

    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
