# Standard FFA Native Port Report

Issue: #25

## Scope

The production native server now has a C implementation for standard games
with three or more snakes. Two-snake standard/solo games still route to duel
minimax. Timeout and error fallback remains first-safe.

The native port intentionally excludes the LightGBM opponent prior because #23
was a no-go. It ships the deterministic/uniform-prior Standard FFA pipeline
with tuned weights from #22 and the conservative #24 depth-search guardrail.

## Implementation

Added:

- `battlesnake/c-core/core/standard_ffa.c`
- `battlesnake/c-core/core/standard_ffa.h`
- Python extension function `standard_ffa_move(...)` for parity tests
- native server routing in `battlesnake_strategy.c`
- arena option `--candidate-strategy standard-v1-native`

Build lists updated:

- `setup.py`
- `tools/build_native_server.sh`
- `tools/run_c_server_tests.sh`

Runbook updated:

- `docs/runbooks/battlesnake-deploy.md`

## Parity

`tests/test_issue_25_standard_ffa_native.py` compares Python `StrategyStandard`
against native `standard_ffa_move` on a representative corpus:

- open corner safety;
- larger-space choice;
- hungry food;
- contested equal-length food;
- #24 corridor trap with frozen-snake deepening.

All selected moves match on the corpus.

The native scorer is a C translation of the frozen deterministic strategy
shape, not the Python telemetry implementation. Candidate score parity is kept
qualitative in this port; selected-move parity is the enforced contract.

## Arena Confirmation

Native C candidate vs Python Standard FFA baseline:

```bash
python3 tools/standard_ffa_arena.py --games 24 --max-turns 80 --seed 71000 --candidate-strategy standard-v1-native --candidate-theta configs/evaluation_weights/standard-ffa-v1-tuned.json --baseline-strategy standard-v1 --baseline-theta configs/evaluation_weights/standard-ffa-v1-tuned.json --output /tmp/issue25-native-vs-python-71000.json --summary-output /tmp/issue25-native-vs-python-71000.txt
```

Result:

```text
candidate objective=0.8311 placement_score=0.6896 latency_p95=0.295 gate=True
baseline  objective=0.7979 placement_score=0.6604 latency_p95=60.072 gate=True
paired mean_placement_delta=0.042 score_delta=0.0292
candidate placements={'1': 11, '2': 9, '3': 3, '4': 1} deaths={'alive': 13, 'won': 11}
baseline placements={'1': 9, '2': 11, '3': 4, '4': 0} deaths={'alive': 15, 'won': 9}
```

This local batch shows no placement regression and large latency headroom.

## Latency

Representative 4-snake board microbenchmark:

```text
C native: count=200 p50=0.22737849940313026 ms p95=0.2385299994784873 ms max=0.2767059995676391 ms
Python:   count=50  p50=2.012022000599245 ms   p95=2.1210180002526613 ms max=2.144579000741942 ms
```

The arena batch is the stricter end-to-end signal: native p95 was `0.295 ms`
while Python Standard FFA p95 was `60.072 ms`.

## Verification

Commands run:

```bash
python3 setup.py build_ext --inplace --force
python3 -m pytest tests/test_issue_25_standard_ffa_native.py
python3 -m pytest tests/test_issue_21_standard_ffa_arena.py tests/test_issue_25_standard_ffa_native.py
tools/run_c_server_tests.sh
tools/build_native_server.sh
```

## Deployment

The native image was built locally through `tools/build_native_server.sh`. Live
deployment still follows `docs/runbooks/battlesnake-deploy.md`: build the image
on `ya.sergeiscv.ru`, restart `playing-battlesnake`, run health and FFA move
smokes, then observe ladder logs for timeout/error/fallback regressions.

The Python dev snake remains deployable for the next iteration cycle.
