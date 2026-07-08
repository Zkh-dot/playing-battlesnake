# Standard FFA LightGBM Prior A/B

Issue #23 tested the trained LightGBM opponent prior against the deterministic
uniform-safe prior at the root of `StrategyStandard`.

Current wiring note: this historical no-go was superseded when `standard-v1`
was changed to use `opponent_prior="model"` directly. Missing artifacts,
missing ML dependencies, inference errors, and prior timeouts still fall back to
the uniform-safe prior.

## Runtime

The committed artifact `ai-artifacts/opponent-model/gbdt_lightgbm.joblib.gz`
embeds `_sklearn_version` `1.9.0`, so warning-free loading requires Python 3.11
with the pinned ML runtime in `ai-artifacts/opponent-model/requirements-ml.lock`.

Artifact load check:

```bash
PYTHONPATH=/tmp/battlesnake-ml311 python3.11 -W error - <<'PY'
from pathlib import Path
import joblib

model = joblib.load(Path("ai-artifacts/opponent-model/gbdt_lightgbm.joblib.gz"))
print(type(model))
print(getattr(model, "classes_", None))
PY
```

Output:

```text
<class 'lightgbm.sklearn.LGBMClassifier'>
[0 1]
```

## A/B Command

Each batch compared model-prior `StrategyStandard` against uniform-prior
`StrategyStandard`, both using the #22 tuned theta:

```bash
PYTHONPATH=/tmp/battlesnake-ml311:. python3.11 tools/standard_ffa_arena.py \
  --games 24 \
  --max-turns 80 \
  --seed 41000 \
  --candidate-theta configs/evaluation_weights/standard-ffa-v1-tuned.json \
  --baseline-theta configs/evaluation_weights/standard-ffa-v1-tuned.json \
  --baseline-strategy standard-v1 \
  --baseline-opponent-prior uniform \
  --candidate-opponent-prior model \
  --output /tmp/issue23-ab-41000.json \
  --summary-output /tmp/issue23-ab-41000.txt
```

The same command shape was run for seeds `41000`, `43000`, `45000`, `47000`,
`49000`, and `51000`.

## Results

| seed | model objective | uniform objective | objective delta | placement score delta | model latency p95 ms | uniform latency p95 ms |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `41000` | `0.7442` | `0.6843` | `+0.0599` | `+0.0604` | `16.416` | `0.781` |
| `43000` | `0.5737` | `0.6229` | `-0.0493` | `-0.0521` | `12.841` | `0.795` |
| `45000` | `0.7291` | `0.6614` | `+0.0677` | `+0.0708` | `13.755` | `0.843` |
| `47000` | `0.6213` | `0.7118` | `-0.0905` | `-0.0958` | `14.321` | `0.912` |
| `49000` | `0.6379` | `0.6395` | `-0.0016` | `+0.0021` | `14.452` | `0.892` |
| `51000` | `0.6108` | `0.6497` | `-0.0389` | `-0.0479` | `14.788` | `0.861` |

Aggregate across 144 paired games:

| metric | model prior | uniform prior | delta |
| --- | ---: | ---: | ---: |
| average objective | `0.6528` | `0.6616` | `-0.0088` |
| placement score delta mean | | | `-0.0104` |
| placement score delta CI95 | | | `[-0.0560, 0.0352]` |
| placement delta mean | | | `-0.0208` |
| placement delta CI95 | | | `[-0.1375, 0.0958]` |

Death causes:

| side | alive | won | unknown |
| --- | ---: | ---: | ---: |
| model prior | `114` | `27` | `3` |
| uniform prior | `111` | `30` | `3` |

Latency:

- Model prior p95 stayed under the `80 ms` hard gate in every batch.
- Model prior added roughly `12-16 ms` p95 over uniform in this Python runtime.

## Verdict

No-go for enabling the LightGBM prior in the dev snake.

The model prior did not improve the aggregate placement objective and produced
fewer wins than the uniform-safe prior on this arena A/B, while unknown deaths
were tied. The runtime module and arena switches remain available for future
experiments, but `standard-v1-model-prior` is intentionally not registered as a
dev-snake variant.

This verdict is the #14 input: do not build the C inference path from this model
until a revised prior or deeper-search integration wins a paired arena A/B.
