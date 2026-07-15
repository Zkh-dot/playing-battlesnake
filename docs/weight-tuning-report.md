# Weight Tuning Report

Dataset source:

- `exports/andreammm_games`

Search configuration:

- optimizer: Optuna TPE
- requested trials: 300
- completed trials before plateau stop: 192
- fixed depth: 3
- time budget per minimax call: 5000 ms
- best trial: 73
- best replay score: 0.5841788478073947

Promoted opponent-pressure weights:

```json
{
  "opponent_low_health_food_denial": 0.9747950851938381,
  "opponent_reachable_space": 0.41419471771599625,
  "opponent_safe_moves": 71.36454529204393,
  "territory_delta": 1.8353673505986556
}
```

Compute-node match comparison:

- node: `scv@192.168.1.6`
- matches: 20 generated standard duel boards
- before/default wins: 8
- after/tuned wins: 11
- draws: 1
- before errors: 0
- after errors: 0
- average turns: 107.15

Interpretation:

- The 20-match result is directional, not conclusive. Excluding the draw, tuned weights won 11 of 19 decided games (57.9%), so sampling noise is still high. Use 100+ generated matches or replay-grounded games before treating this as a stable strength estimate.

## Issue #46 balanced promotion evidence

The later reproducible gate is recorded in
`docs/duel-weight-ab-report.md`, with raw compact artifacts in
`docs/evidence/issue-46-duel-weight-ab.json` and
`docs/evidence/issue-46-duel-weight-replays.json`. It ran 200 paired games (100
seeded boards with physical sides and search order balanced) and a four-position
replay-risk check with 40 records. The candidate showed a positive paired
signal, but failed the predeclared relative latency gate: candidate p99 was
110.37% of default, above the 110% limit. The decision is therefore
**inconclusive — do not promote**.

`tuned-opponent-pressure@1` remains a `candidate`, not the production default.
The generated production default remains `duel-default@1`. Configuration
plumbing and promotion are separate decisions; any promotion requires a
separate promotion PR with reviewed evidence and an explicit default/status
change.

Reproduce the balanced match report with the frozen settings:

```bash
python3 tools/tuning/compare_weights_matches.py \
  --before-weights configs/evaluation_weights/default.json \
  --after-weights configs/evaluation_weights/tuned-opponent-pressure.json \
  --seed 46001 --scenario-count 100 --fixed-depth 3 \
  --time-budget-ms 300 --max-turns 200 \
  --output docs/evidence/issue-46-duel-weight-ab.json \
  --markdown-output /tmp/issue-46-duel-weight-ab.generated.md
```

Reproduce the four diagnostic replays (both profiles are selected by default):

```bash
python3 tools/tuning/report_duel_weight_replays.py \
  --fixtures tests/fixtures/issue_46_duel_weight_replays.json \
  --budget-ms 300 --repeats 5 \
  --output docs/evidence/issue-46-duel-weight-replays.json
```

These replay positions are diagnostics, not universal expected-move
assertions. Regenerate the Markdown summary from the resulting artifacts and
compare it with `docs/duel-weight-ab-report.md`; artifact hashes and the exact
host/toolchain provenance are recorded there.

| match | scenario | after side | winner | turns |
| ---: | --- | ---: | --- | ---: |
| 0 | generated_standard_duel_0 | 0 | after | 104 |
| 1 | generated_standard_duel_1 | 1 | after | 110 |
| 2 | generated_standard_duel_2 | 0 | after | 108 |
| 3 | generated_standard_duel_3 | 1 | after | 120 |
| 4 | generated_standard_duel_4 | 0 | before | 108 |
| 5 | generated_standard_duel_5 | 1 | before | 107 |
| 6 | generated_standard_duel_6 | 0 | before | 104 |
| 7 | generated_standard_duel_7 | 1 | after | 111 |
| 8 | generated_standard_duel_8 | 0 | before | 110 |
| 9 | generated_standard_duel_9 | 1 | after | 104 |
| 10 | generated_standard_duel_10 | 0 | before | 107 |
| 11 | generated_standard_duel_11 | 1 | after | 103 |
| 12 | generated_standard_duel_12 | 0 | before | 106 |
| 13 | generated_standard_duel_13 | 1 | after | 106 |
| 14 | generated_standard_duel_14 | 0 | after | 105 |
| 15 | generated_standard_duel_15 | 1 | draw | 109 |
| 16 | generated_standard_duel_16 | 0 | before | 104 |
| 17 | generated_standard_duel_17 | 1 | before | 109 |
| 18 | generated_standard_duel_18 | 0 | after | 103 |
| 19 | generated_standard_duel_19 | 1 | after | 105 |
