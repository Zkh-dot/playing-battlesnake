# Issue #46 duel weight promotion evidence

## Decision

**Inconclusive — do not promote `tuned-opponent-pressure@1`.** This change only makes the candidate selectable and auditable. The generated production default remains `duel-default@1`; promotion requires a separate pull request.

The candidate won 102 games to the default's 96, with two draws, but the paired unit is the generated starting board rather than an individual game. Of 100 paired boards, the candidate swept 9, the default swept 6, 83 split, and 2 contained a draw. The candidate sweep share among 15 decisive sweeps was 60%, but its Wilson 95% interval was 35.75%–80.18% and the exact two-sided sign-test p-value was 0.6072. That is not a positive paired signal with uncertainty.

## Frozen experiment

- Experiment input commit: `b44198a985e538500316076a7ed4cf871422b209`
- Host: `sergei-scv-lin`, Linux `6.8.0-124-generic`, x86_64
- CPU: 13th Gen Intel Core i5-1340P, 12 cores / 16 logical CPUs
- Python: 3.10.12
- Compiler used for the forced native rebuild: GCC 11.4.0
- Wall time: 323.55 s; user CPU: 323.40 s; max RSS: 120,960 KiB
- Compute-node note: `scv@192.168.1.6` was attempted first but was unavailable with `No route to host`; the sample was not reduced.

Profiles:

| Role | Profile | Canonical weights SHA-256 | Source-file SHA-256 |
| --- | --- | --- | --- |
| default | `duel-default@1` | `a51a2213f403f1e21ccb4eb928927bfac72a11acd7e1d52de2f88ef2277f9629` | `25a9ce68854166337407a3fe62772866f92d790f4d797b28f615b469b640e66a` |
| candidate | `tuned-opponent-pressure@1` | `6996443271beb18e835355a72ad02e2ee9d47148a33e14370ce096fc766f1c5d` | `f6cbb60fa4edb9556f3464d2ccecd49b9f0f4dd6ac210c12015038636f40126b` |

Command:

```bash
python3 tools/tuning/compare_weights_matches.py \
  --before-weights configs/evaluation_weights/default.json \
  --after-weights configs/evaluation_weights/tuned-opponent-pressure.json \
  --seed 46001 --scenario-count 100 --fixed-depth 3 \
  --time-budget-ms 300 --max-turns 200 \
  --output docs/evidence/issue-46-duel-weight-ab.json \
  --markdown-output /tmp/issue-46-duel-weight-ab.generated.md
```

The seed creates 100 ordinary Standard duel boards. Every board is played exactly twice, with both profile and physical starting sides swapped. Each profile therefore played 100 games from physical side 0 and 100 from side 1. The raw artifact contains all 200 match rows and 40,772 move rows; headline metrics are recomputed from those rows in the automated evidence test.

## A/B results

| Metric | Default | Candidate |
| --- | ---: | ---: |
| wins / losses / draws | 96 / 102 / 2 | 102 / 96 / 2 |
| terminal survivals | 96 | 102 |
| total turns survived | 20,386 | 20,386 |
| alive at 200-turn cap | 0 | 0 |
| search selections | 20,386 | 20,386 |
| search errors / audit errors | 0 / 0 | 0 / 0 |
| search timeouts | 0 | 0 |
| structural-risk selections | 0 | 0 |
| independent policy violations | 0 | 0 |
| native elapsed p50 / p95 / p99 / max, ms | 1.577 / 11.439 / 14.818 / 32.373 | 2.490 / 12.740 / 16.289 / 37.563 |

Latency is the native `minimax_diagnostics.elapsed_ms` value for each selected move, not Python wrapper wall time. Percentiles use nearest-rank semantics. Candidate p99 was 109.92% of default p99 and remained below both 300 ms and the predeclared 110% relative ceiling.

“Structural risk” means selecting a capacity-deficient, non-safe root candidate while at least one safe live-reply alternative exists. The independent violation count comes from `tools.check_duel_structural_policy.audit_diagnostics`; it does not participate in move selection.

## Replay-risk diagnostics

The replay run used a 300 ms budget, five repeats, and both named profiles. All 40 records completed without errors. Every run exhausted the iterative-deepening budget (`timed_out=true`) near 300 ms, but each game/profile combination returned one stable move across its five repeats. Every selected root had `structural_proof=safe`; structural-risk selections and independent policy violations were both zero.

| Game / turn | Recorded move | Default move / depth | Candidate move / depth |
| --- | --- | --- | --- |
| `1197cf21…` / 197 | up | left / 9 | left / 8 |
| `8fd97d0d…` / 357 | down | right / 8 | right / 9 |
| `ab3c8a6f…` / 326 | right | left / 10 | left / 9 |
| `be95288a…` / 308 | down | up / 10 | up / 9 |

These positions are diagnostics, not universal expected-move assertions. The compact fixture stores the board, ruleset, snake identity, source game/turn, and recorded move only; it does not encode a required replacement move.

Command:

```bash
python3 tools/tuning/report_duel_weight_replays.py \
  --fixtures tests/fixtures/issue_46_duel_weight_replays.json \
  --budget-ms 300 --repeats 5 \
  --output docs/evidence/issue-46-duel-weight-replays.json
```

## Predeclared gate

| Criterion | Result |
| --- | --- |
| zero search/audit errors | pass: 0 for both profiles |
| no increase in independent policy violations | pass: 0 vs 0 |
| no material structural-risk increase | pass: 0 vs 0 |
| non-inferior survival | pass descriptively: 102 vs 96 terminal survivals; equal turns survived |
| positive paired win signal with uncertainty | **not established**: 9 vs 6 sweeps, interval crosses 50%, p=0.6072 |
| candidate p99 ≤300 ms and ≤110% of default | pass: 16.289 ms and 109.92% |

Because the paired win criterion is not established, the gate is inconclusive and the candidate must not be promoted.

## Limitations

- This is one deterministic seed over generated ordinary Standard boards, not a sample of the live ladder distribution.
- Fixed depth 3 makes the comparison reproducible but does not model production iterative-deepening depth distribution.
- Split pairs dominate (83/100), leaving only 15 decisive sweeps for the paired sign test. Treating 200 games as independent would be pseudo-replication.
- The Wilson interval describes the candidate share among decisive sweeps; it excludes split and draw-containing pairs and is reported with that limitation.
- The four replay positions are a targeted risk check. Stable moves under these settings do not prove those moves universally optimal.

Raw evidence:

- `docs/evidence/issue-46-duel-weight-ab.json` — SHA-256 `0f12cc9c81015198dd98a290afcf0071468677ed121a684a2ada48fdfff36da6`
- `docs/evidence/issue-46-duel-weight-replays.json` — SHA-256 `bf66d5fcc896e68a263f9a5caa4836f0c8f695506c8a3e8c999ae75d2d93c54e`
