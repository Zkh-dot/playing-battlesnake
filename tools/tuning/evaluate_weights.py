from __future__ import annotations

import argparse
import json
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Callable, Iterable, Protocol

from battlesnake.battlesnake_native import minimax_diagnostics
from tools.tuning.replay_dataset import ReplaySample, export_paths, iter_replay_samples
from tools.tuning.duel_weight_profiles import load_profile


class SampleLike(Protocol):
    split: str
    game_id: str
    turn: int
    snake_id: str
    snake_name: str
    target_move: str
    board: object


MinimaxFn = Callable[..., dict[str, object]]


@dataclass(frozen=True)
class EvaluationMetrics:
    samples: int
    matches: int
    mismatches: int
    errors: int
    timeouts: int

    @property
    def accuracy(self) -> float:
        if self.samples == 0:
            return 0.0
        return self.matches / self.samples

    @property
    def error_rate(self) -> float:
        if self.samples == 0:
            return 0.0
        return self.errors / self.samples

    @property
    def timeout_rate(self) -> float:
        if self.samples == 0:
            return 0.0
        return self.timeouts / self.samples

    @property
    def score(self) -> float:
        return self.accuracy - 0.10 * self.error_rate - 0.02 * self.timeout_rate

    def to_dict(self) -> dict[str, float | int]:
        data = asdict(self)
        data["accuracy"] = self.accuracy
        data["error_rate"] = self.error_rate
        data["timeout_rate"] = self.timeout_rate
        data["score"] = self.score
        return data


def evaluate_samples(
    samples: Iterable[SampleLike],
    *,
    weights: dict[str, float],
    fixed_depth: int,
    time_budget_ms: int,
    minimax_fn: MinimaxFn = minimax_diagnostics,
) -> EvaluationMetrics:
    total = 0
    matches = 0
    mismatches = 0
    errors = 0
    timeouts = 0

    for sample in samples:
        total += 1
        try:
            diagnostics = minimax_fn(
                sample.board,
                sample.snake_id,
                time_budget_ms=time_budget_ms,
                fixed_depth=fixed_depth,
                enable_tt=True,
                enable_move_ordering=True,
                enable_make_unmake=True,
                weights=weights,
            )
            predicted = str(diagnostics["move"])
            if diagnostics.get("timed_out"):
                timeouts += 1
            if predicted == sample.target_move:
                matches += 1
            else:
                mismatches += 1
        except Exception:
            errors += 1

    return EvaluationMetrics(
        samples=total,
        matches=matches,
        mismatches=mismatches,
        errors=errors,
        timeouts=timeouts,
    )


def load_weights(path: Path) -> dict[str, float]:
    return dict(load_profile(path).weights)


def load_samples(exports_root: Path, split: str, limit: int | None) -> list[ReplaySample]:
    samples = [sample for sample in iter_replay_samples(export_paths(exports_root)) if sample.split == split]
    if limit is not None:
        return samples[:limit]
    return samples


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--exports", type=Path, default=Path("exports"))
    parser.add_argument("--weights", type=Path, required=True)
    parser.add_argument("--split", choices=["train", "validation", "test"], default="validation")
    parser.add_argument("--fixed-depth", type=int, default=3)
    parser.add_argument("--time-budget-ms", type=int, default=5000)
    parser.add_argument("--limit", type=int)
    args = parser.parse_args()

    weights = load_weights(args.weights)
    samples = load_samples(args.exports, args.split, args.limit)
    metrics = evaluate_samples(
        samples,
        weights=weights,
        fixed_depth=args.fixed_depth,
        time_budget_ms=args.time_budget_ms,
    )
    print(json.dumps(metrics.to_dict(), sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
