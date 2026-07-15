from __future__ import annotations

import argparse
import json
import random
import sys
from pathlib import Path
from typing import Any

from tools.tuning.duel_weight_profiles import DuelWeightProfile, load_profile, validate_profile
from tools.tuning.evaluate_weights import evaluate_samples, load_samples


DEFAULT_SEARCH_SPACE: dict[str, tuple[float, float]] = {
    "opponent_reachable_space": (0.0, 12.0),
    "territory_delta": (0.0, 12.0),
    "opponent_safe_moves": (0.0, 80.0),
    "opponent_low_health_food_denial": (0.0, 10.0),
}


def suggest_random_candidate(seed: int) -> dict[str, float]:
    rng = random.Random(seed)
    return {
        key: rng.uniform(lower, upper)
        for key, (lower, upper) in DEFAULT_SEARCH_SPACE.items()
    }


def merge_candidate_weights(
    default_weights: dict[str, float],
    candidate: dict[str, float],
) -> dict[str, float]:
    merged = dict(default_weights)
    merged.update(candidate)
    return merged


def _run_random_search(
    *,
    default_weights: dict[str, float],
    samples: list[Any],
    trials: int,
    fixed_depth: int,
    time_budget_ms: int,
    output: Path,
) -> dict[str, float]:
    best_score = float("-inf")
    best_weights = dict(default_weights)
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w") as stream:
        for trial_index in range(trials):
            candidate = suggest_random_candidate(seed=trial_index)
            weights = merge_candidate_weights(default_weights, candidate)
            metrics = evaluate_samples(
                samples,
                weights=weights,
                fixed_depth=fixed_depth,
                time_budget_ms=time_budget_ms,
            )
            row = {
                "trial": trial_index,
                "score": metrics.score,
                "metrics": metrics.to_dict(),
                "candidate": candidate,
            }
            stream.write(json.dumps(row, sort_keys=True) + "\n")
            stream.flush()
            if metrics.score > best_score:
                best_score = metrics.score
                best_weights = weights
    return best_weights


def _run_optuna_search(
    *,
    default_weights: dict[str, float],
    samples: list[Any],
    trials: int,
    fixed_depth: int,
    time_budget_ms: int,
    storage: str,
    study_name: str,
    plateau_patience: int,
    plateau_min_delta: float,
) -> dict[str, float]:
    import optuna

    study = optuna.create_study(
        direction="maximize",
        storage=storage,
        study_name=study_name,
        load_if_exists=True,
        sampler=optuna.samplers.TPESampler(seed=42),
    )

    def objective(trial: optuna.Trial) -> float:
        candidate = {
            key: trial.suggest_float(key, lower, upper)
            for key, (lower, upper) in DEFAULT_SEARCH_SPACE.items()
        }
        weights = merge_candidate_weights(default_weights, candidate)
        metrics = evaluate_samples(
            samples,
            weights=weights,
            fixed_depth=fixed_depth,
            time_budget_ms=time_budget_ms,
        )
        trial.set_user_attr("metrics", metrics.to_dict())
        trial.set_user_attr("candidate", candidate)
        print(
            json.dumps(
                {
                    "trial": trial.number,
                    "score": metrics.score,
                    "accuracy": metrics.accuracy,
                    "errors": metrics.errors,
                    "timeouts": metrics.timeouts,
                },
                sort_keys=True,
            ),
            flush=True,
        )
        return metrics.score

    best_seen = {"value": float("-inf"), "stale": 0}
    try:
        best_seen["value"] = float(study.best_value)
    except ValueError:
        pass

    def stop_on_plateau(study: optuna.Study, trial: optuna.trial.FrozenTrial) -> None:
        if plateau_patience <= 0 or trial.value is None:
            return
        value = float(trial.value)
        if value > best_seen["value"] + plateau_min_delta:
            best_seen["value"] = value
            best_seen["stale"] = 0
            return
        best_seen["stale"] += 1
        if best_seen["stale"] >= plateau_patience:
            print(
                json.dumps(
                    {
                        "event": "plateau_stop",
                        "best_score": best_seen["value"],
                        "patience": plateau_patience,
                        "trial": trial.number,
                    },
                    sort_keys=True,
                ),
                flush=True,
            )
            study.stop()

    if plateau_patience > 0:
        study.optimize(objective, n_trials=trials, callbacks=[stop_on_plateau])
    else:
        study.optimize(objective, n_trials=trials)
    best_candidate = {
        key: float(study.best_trial.params[key])
        for key in DEFAULT_SEARCH_SPACE
    }
    best_weights = merge_candidate_weights(default_weights, best_candidate)
    return best_weights


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--exports", type=Path, default=Path("exports"))
    parser.add_argument("--default-weights", type=Path, default=Path("configs/evaluation_weights/default.json"))
    parser.add_argument("--split", choices=["train", "validation", "test"], default="train")
    parser.add_argument("--fixed-depth", type=int, default=3)
    parser.add_argument("--time-budget-ms", type=int, default=5000)
    parser.add_argument("--limit", type=int)
    parser.add_argument("--trials", type=int, default=100)
    parser.add_argument("--storage", default="sqlite:///artifacts/weight_tuning/optuna.db")
    parser.add_argument("--study-name", default="opponent-pressure-v1")
    parser.add_argument("--output", type=Path, default=Path("artifacts/weight_tuning/best_weights.json"))
    parser.add_argument("--plateau-patience", type=int, default=50)
    parser.add_argument("--plateau-min-delta", type=float, default=0.0)
    parser.add_argument("--random-fallback", action="store_true")
    args = parser.parse_args()

    base_profile = load_profile(args.default_weights)
    default_weights = dict(base_profile.weights)
    samples = load_samples(args.exports, args.split, args.limit)
    use_random_search = args.random_fallback
    if not use_random_search:
        try:
            import optuna  # noqa: F401
        except ImportError:
            use_random_search = True
            print(
                "optuna is not installed; falling back to deterministic random search",
                file=sys.stderr,
            )

    if use_random_search:
        best_weights = _run_random_search(
            default_weights=default_weights,
            samples=samples,
            trials=args.trials,
            fixed_depth=args.fixed_depth,
            time_budget_ms=args.time_budget_ms,
            output=args.output.with_name(f"{args.output.stem}-trials.jsonl"),
        )
    else:
        best_weights = _run_optuna_search(
            default_weights=default_weights,
            samples=samples,
            trials=args.trials,
            fixed_depth=args.fixed_depth,
            time_budget_ms=args.time_budget_ms,
            storage=args.storage,
            study_name=args.study_name,
            plateau_patience=args.plateau_patience,
            plateau_min_delta=args.plateau_min_delta,
        )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    candidate_profile = validate_profile(DuelWeightProfile(
        schema_version=base_profile.schema_version,
        name=f"{base_profile.name}-search-candidate",
        version=base_profile.version,
        status="candidate",
        weights=best_weights,
    ))
    args.output.write_text(json.dumps(candidate_profile.to_dict(), indent=2) + "\n")
    print(json.dumps({"output": str(args.output)}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
