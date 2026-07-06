# Standard FFA Opponent Model Runtime Report

## Summary

Offline Standard FFA opponent-move model is viable as a move-prior component.
It predicts `P(opponent_move | board, opponent snake, candidate_move)` for each
alive non-controlled snake, not our own best move directly.

Best trained model:

- Model family: `gbdt`
- Training engine: `polars+lightgbm`
- Model artifact: `ai-artifacts/opponent-model/gbdt_lightgbm.joblib`
- Training host: `scv@192.168.1.6`
- Train rows: `6,292,684`
- Validation rows: `1,311,972`
- Test rows: `1,352,660`
- Candidate moves per observation: `4`

The model should be treated as an opponent policy prior for Standard FFA. It
should not replace safety checks, flood-fill/space heuristics, or future FFA
search. Its useful job is to bias predicted opponent joint moves away from a
uniform prior and toward empirically likely moves.

## Dataset

Source archive:

- `exports/battlesnake_top150_games_gt50.zip`
- SHA-256: `a31b445c152a33cbaf1617791babc744ec0b4511ef17eb8ee8b298cc56c7cd90`

Extracted dataset:

- Replays: `3,720`
- Observations: `2,239,329`
- Candidate rows: `8,957,316`
- Snakes: `14,873`

Split by candidate rows:

```json
{
  "test": 1352660,
  "train": 6292684,
  "validation": 1311972
}
```

Target move distribution:

```json
{
  "down": 583821,
  "left": 534624,
  "right": 532646,
  "up": 588238
}
```

Split leakage check:

- `leaked_games 0`
- Split is deterministic by `game_id`.

## Offline Metrics

Validation top-1 accuracy:

| Model | Validation top-1 | Observations |
|---|---:|---:|
| `move_prior` | `0.2630` | `327,993` |
| `logistic` | `0.6210` | `327,993` |
| `gbdt` | `0.6382` | `327,993` |

Selected model test metrics:

| Metric | Value |
|---|---:|
| Best model | `gbdt` |
| Test top-1 accuracy | `0.6299` |
| Test grouped negative log likelihood | `0.7154` |
| Test observations | `338,165` |

Acceptance gates:

| Gate | Required | Measured | Result |
|---|---:|---:|---|
| Validation lift over move prior | `>= 0.0800` | `0.3751` | pass |
| Test drop from validation | `<= 0.0400` | `0.0083` | pass |
| Eligible snakes above 0.35 top-1 | `>= 0.8000` | `0.9949` over `1,377` snakes | pass |
| Split leakage | `0` games | `0` games | pass |

Decision:

- Accepted for future runtime-design discussion.

## Runtime Latency

Latency was measured for model inference on already-built feature rows. These
numbers exclude board-to-feature extraction, Python web handler overhead,
serialization, and any FFI/native integration cost.

One observation means one opponent snake with four candidate moves.

### Local Machine

Host:

- `sergei-scv-lin`
- CPU count: `16`
- RAM: `15 GiB`
- Python: `3.10.12`

| Threads | 1 observation median / p95 | 3 observations median / p95 | 10 observations median / p95 | 1000 observations median / p95 |
|---:|---:|---:|---:|---:|
| 1 | `3.49 / 5.26 ms` | `7.08 / 10.16 ms` | `15.28 / 18.55 ms` | `886.74 / 1204.19 ms` |
| 2 | `3.66 / 4.41 ms` | `4.34 / 5.47 ms` | `8.28 / 11.17 ms` | `627.99 / 741.54 ms` |
| 4 | `3.12 / 3.99 ms` | `4.32 / 5.09 ms` | `9.33 / 12.35 ms` | `455.30 / 499.56 ms` |
| 8 | `3.79 / 4.15 ms` | `4.89 / 5.50 ms` | `6.14 / 7.07 ms` | `282.01 / 307.68 ms` |
| 24 | `3.80 / 6.33 ms` | `3.81 / 7.88 ms` | `7.25 / 13.15 ms` | `222.77 / 236.72 ms` |

Note: local benchmark loaded a model trained with `scikit-learn 1.9.0` under a
local environment with `scikit-learn 1.7.2`, which produced an
`InconsistentVersionWarning`. The LightGBM booster still ran and the benchmark
is useful for latency shape, but production should use a pinned compatible
runtime.

### ya.sergeiscv.ru

Host:

- `compute-vm-4-8-100-ssd-1759320606973`
- CPU count: `4`
- RAM: `7.8 GiB`
- Python: `3.11.2`

| Threads | 1 observation median / p95 | 3 observations median / p95 | 10 observations median / p95 | 1000 observations median / p95 |
|---:|---:|---:|---:|---:|
| 1 | `4.22 / 5.72 ms` | `6.76 / 7.25 ms` | `15.24 / 15.96 ms` | `1103.58 / 1128.78 ms` |
| 2 | `3.45 / 3.71 ms` | `4.84 / 5.38 ms` | `9.35 / 10.36 ms` | `549.78 / 589.80 ms` |
| 4 | `3.94 / 4.79 ms` | `4.70 / 5.43 ms` | `7.39 / 8.40 ms` | `321.37 / 420.97 ms` |
| 8 | `3.57 / 5.18 ms` | `4.75 / 6.60 ms` | `8.58 / 11.58 ms` | `335.79 / 426.04 ms` |
| 24 | `3.52 / 4.11 ms` | `4.53 / 5.23 ms` | `7.45 / 8.45 ms` | `457.78 / 510.11 ms` |

Practical online setting:

- For one to three opponent observations per turn, `2` to `4` threads is the
  sensible range on `ya.sergeiscv.ru`.
- Expected model-only cost for a normal Standard FFA turn is about `4.5-5.5 ms`
  median/p95 when scoring three opponents.
- `24` threads is counterproductive for online tiny batches and should be
  reserved only for offline batch scoring.

### Compute Node

Host:

- `scv@192.168.1.6`
- CPU count: `24`
- RAM: `62 GiB`

Compute-node numbers were useful for training and smoke checks but are
optimistic for runtime deployment.

For reference, model-only latency there was around:

- 1 observation, 1 thread: `1.37 ms` median, `1.44 ms` p95
- 3 observations, 4 threads: `1.31 ms` median, `1.44 ms` p95
- 10 observations, 24 threads: `1.45 ms` median, `1.53 ms` p95

## What The Model Can Do

The model can:

- Score each candidate move for each opponent snake in Standard FFA positions.
- Produce a non-uniform opponent move prior that is much stronger than simple
  global move frequency.
- Rank the actual next opponent move first on held-out games about `63%` of the
  time.
- Provide per-snake grouped metrics because `snake_id` remains in metadata.
- Run quickly enough for a Python-side Standard FFA policy layer if the rest of
  the turn budget leaves roughly `5-10 ms` for opponent scoring.

The model cannot:

- Choose our move by itself.
- Replace collision/safety rules.
- Prove that a move is safe under adversarial response.
- Generalize to rulesets outside Standard FFA.
- Guarantee quality on completely different player populations; the split is
  by game, not by player.
- Be called inside a deep native search node expansion without a more efficient
  C/C++ inference path or aggressive caching.

## Recommended Runtime Integration

Current repo state:

- `battlesnake/strategies/standard.py` is still a stub.
- The native server intentionally applies minimax only to solo-ruleset 1v1
  duels and uses a safe fallback for standard/4+ snake games.

The correct integration point is therefore a future `StrategyStandard`
implementation, before any native FFA search exists.

Recommended pipeline:

1. Build a Standard FFA board snapshot from the incoming game state.
2. Enumerate alive opponent snakes.
3. For each opponent, build exactly four candidate feature rows using the same
   feature definitions as `battlesnake.training.opponent_model.features`.
4. Score the rows with the loaded GBDT model.
5. Normalize each opponent's four scores into a move distribution.
6. Use the distributions as opponent priors in our move evaluation.

The first production use should be shallow and conservative:

- Generate top-k likely joint opponent moves rather than all `4^N`
  combinations.
- For each candidate move of our snake, simulate/evaluate a small number of
  likely opponent joint responses.
- Keep deterministic safety/flood-fill checks as hard filters.
- Use model priors only to weight or order opponent responses, not to ignore
  low-probability lethal moves.

Concrete places to connect:

- Python strategy layer: add an `OpponentPolicy`/`OpponentMovePrior` helper used
  by `StrategyStandard.decide`.
- Candidate feature extraction: reuse or mirror
  `battlesnake.training.opponent_model.features.candidate_rows`; if runtime
  imports from `training` are undesirable, move shared feature code into a
  runtime-safe module.
- Search/evaluation layer: use the model output as a prior for opponent move
  ordering or scenario weighting, not as a final action selector.

Avoid this initially:

- Calling Python LightGBM inside native minimax expansion for every node.
- Expanding full opponent joint action space uniformly.
- Replacing safe-move logic with the model's top prediction.

## Runtime Engineering Requirements

Before enabling in games, implement and measure:

- Model load once at process startup, not per turn.
- Runtime feature extraction latency from real `GameState`/`Board`.
- End-to-end `StrategyStandard.decide` latency on representative Standard FFA
  states.
- Fallback behavior when model artifact is missing or inference fails.
- Version pinning for LightGBM/scikit-learn/joblib runtime compatibility.
- Regression tests that compare offline feature rows and runtime feature rows
  for the same board state.

Suggested first latency budget:

- Model inference: `<= 6 ms` p95 for three opponents on deployment hardware.
- Feature extraction: target `<= 2-4 ms` p95.
- Remaining strategy evaluation: keep total `StrategyStandard.decide` under the
  game timeout with a conservative margin.

## Artifacts

Generated artifacts are intentionally not committed:

- `ai-artifacts/opponent-model/candidate_rows.csv`
- `ai-artifacts/opponent-model/candidate_rows.parquet`
- `ai-artifacts/opponent-model/metrics.json`
- `ai-artifacts/opponent-model/metrics-review.md`
- `ai-artifacts/opponent-model/gbdt_lightgbm.joblib`
- `ai-artifacts/opponent-model/logistic.joblib`
- `ai-artifacts/opponent-model/compute_run_metadata.json`
- `ai-artifacts/opponent-model/compute-requirements-ml.lock`

Committed docs/code record the reproducible pipeline and measured results.
