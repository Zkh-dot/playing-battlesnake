# Issue #23: Standard FFA LightGBM Opponent Prior

## Scope

Add a Python-only dev-snake opponent prior that can use the trained LightGBM artifact at the Standard FFA root. The model may only weight/order opponent scenarios; hard gates, danger injection, and first-safe fallbacks stay independent.

## Current Findings

- Issue #23 requires feature parity, warning-free runtime pins, epsilon smoothing toward uniform-safe, arena A/B, and an explicit go/no-go verdict.
- The committed artifact is `ai-artifacts/opponent-model/gbdt_lightgbm.joblib.gz`.
- The artifact embeds `_sklearn_version` `1.9.0`; local Python 3.10 cannot install `scikit-learn==1.9.0`, so warning-free model validation needs Python 3.11.
- A Python 3.11 target install under `/tmp/battlesnake-ml311` successfully loaded the artifact with `-W error` using:
  - `numpy==2.2.6`
  - `pandas==2.3.3`
  - `scikit-learn==1.9.0`
  - `joblib==1.5.3`
  - `lightgbm==4.6.0`
- The Python 3.11 native extension build succeeded with `PYTHONPATH=/tmp/battlesnake-ml311 python3.11 setup.py build_ext --inplace --force`.

## Implementation

- Add a runtime-safe feature module outside `battlesnake.training`.
  - Keep training `candidate_rows` byte-identical by delegating its feature dict construction to the runtime module.
  - Add parity tests comparing training feature rows and runtime feature rows for the same board.
- Add a model-prior helper.
  - Load the model lazily once from `STANDARD_OPPONENT_MODEL_PATH` or the committed `.joblib.gz`.
  - Return uniform-safe priors if the artifact is missing, dependencies are unavailable, inference fails, or the elapsed model call exceeds `STANDARD_OPPONENT_PRIOR_TIMEOUT_MS`.
  - Filter model probabilities to safe moves, normalize, then smooth with `eps` toward uniform-safe.
- Wire `StrategyStandard`.
  - Default remains uniform unless the model-prior variant is explicitly selected.
  - Register `standard-v1-model-prior` in the dev snake for A/B.
  - Decision telemetry continues to show priors; hard gates and forced danger scenarios still run after priors.
- Extend `tools/standard_ffa_arena.py`.
  - Keep existing default candidate-vs-first-safe behavior.
  - Add CLI switches for candidate/baseline opponent priors and baseline `standard-v1` so #23 can run model prior vs uniform prior on paired seeds.
- Update runtime pins.
  - Record Python 3.11 plus the package versions that load the artifact warning-free.
- Add an A/B report.
  - Commit placement objective delta, death-cause table, latency delta, and explicit go/no-go.
  - Comment on #14 with the verdict input after #23 is complete.

## Verdict

- No-go for enabling the LightGBM prior as a dev-snake variant.
- Six 24-game paired batches, model prior vs uniform prior, both using #22 tuned theta:
  - average model objective `0.6528`;
  - average uniform objective `0.6616`;
  - average objective delta `-0.0088`;
  - placement score delta mean `-0.0104`, CI95 `[-0.0560, 0.0352]`;
  - death causes tied on `unknown` at `3` each;
  - model prior had fewer wins, `27` vs `30`;
  - latency p95 stayed within the `80 ms` gate, but added roughly `12-16 ms`.
- `standard-v1-model-prior` is intentionally not registered in the dev snake.
- Full report: `docs/standard-ffa-lightgbm-prior-ab.md`.

## Verification

- `python3 -m pytest tests/test_issue_23_lightgbm_prior.py`
- `python3 -m pytest tests/test_issue_19_standard_strategy.py tests/test_issue_21_standard_ffa_arena.py tests/test_issue_23_lightgbm_prior.py`
- `PYTHONPATH=/tmp/battlesnake-ml311 python3.11 -W error ...` artifact load smoke for sklearn `1.9.0`.
- Arena A/B command:
  - `PYTHONPATH=/tmp/battlesnake-ml311:. python3.11 tools/standard_ffa_arena.py --baseline-strategy standard-v1 --baseline-opponent-prior uniform --candidate-opponent-prior model --games <N> --seed <S> ...`
