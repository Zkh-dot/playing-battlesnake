# Issue #46 duel weight promotion evidence

## Decision

**inconclusive — do not promote.** The generated production default remains `duel-default@1`; this PR only makes the candidate selectable and auditable, and any promotion requires a separate PR.

The candidate won 109 games to the default's 89, with 2 draws. At the paired-board unit, the candidate swept 14, the default swept 4, 80 split, and 2 contained a draw. The candidate decisive-sweep share was 77.78% (Wilson 95% 54.79%–91.00%; exact two-sided sign-test p=0.0309).

## Frozen experiment

- Experiment input commit: `9322e9c67773583744922301f5b9b7b320e5d7f1`
- Host: `sergei-scv-lin`, Linux `6.8.0-124-generic`, `x86_64`
- CPU: 13th Gen Intel(R) Core(TM) i5-1340P (16 logical CPUs)
- Python: 3.10.12
- Compiler: x86_64-linux-gnu-gcc (Ubuntu 11.4.0-1ubuntu1~22.04.3) 11.4.0
- Tool-recorded wall time: 260.20 s; max RSS: 32,816 KiB

| Role | Profile | Canonical weights SHA-256 | Source-file SHA-256 |
| --- | --- | --- | --- |
| default | `duel-default@1` | `a51a2213f403f1e21ccb4eb928927bfac72a11acd7e1d52de2f88ef2277f9629` | `25a9ce68854166337407a3fe62772866f92d790f4d797b28f615b469b640e66a` |
| candidate | `tuned-opponent-pressure@1` | `6996443271beb18e835355a72ad02e2ee9d47148a33e14370ce096fc766f1c5d` | `f6cbb60fa4edb9556f3464d2ccecd49b9f0f4dd6ac210c12015038636f40126b` |

```bash
python3 tools/tuning/compare_weights_matches.py \
  --before-weights configs/evaluation_weights/default.json \
  --after-weights configs/evaluation_weights/tuned-opponent-pressure.json \
  --seed 46001 --scenario-count 100 --fixed-depth 3 \
  --time-budget-ms 300 --max-turns 200 \
  --output docs/evidence/issue-46-duel-weight-ab.json \
  --markdown-output /tmp/issue-46-duel-weight-ab.generated.md
```

Each of 100 seeded Standard boards was played twice with physical sides swapped. Search order was also paired: each profile was evaluated first exactly once per board. Both searches received the same unchanged board snapshot. Each profile played 100 games from physical side 0 and 100 from side 1.

## A/B results

| Metric | Default | Candidate |
| --- | ---: | ---: |
| wins / losses / draws | 89 / 109 / 2 | 109 / 89 / 2 |
| terminal survivals | 89 | 109 |
| alive at 200-turn cap | 0 | 0 |
| final length total | 758 | 887 |
| search selections | 20399 | 20399 |
| search errors / audit errors | 0 / 0 | 0 / 0 |
| search timeouts | 0 | 0 |
| structural-risk selections | 0 | 0 |
| independent policy violations | 0 | 0 |
| native elapsed p50 / p95 / p99 / max, ms | 1.721 / 9.167 / 11.635 / 17.372 | 1.987 / 10.004 / 12.841 / 18.435 |

Shared match duration was 20,399 turns total (102.00 mean). It is not attributed as profile survival. Profile survival uses terminal survival, alive-at-cap, and final state only.

The compact artifact retains 200 match outcome/final-state rows and 40,798 raw native latency samples. Sparse event arrays retain identifiers for every timeout, search error, audit error, structural risk, and policy violation. All headline metrics are recomputed from these raw structures.

## Replay-risk diagnostics

The replay run used 300 ms, 5 repeats, and both named profiles: 40 records, 0 errors, 40 timeouts, 0 structural risks, and 0 policy violations.

| Game / turn | Recorded move | Default move / depth range | Candidate move / depth range |
| --- | --- | --- | --- |
| `1197cf21…` / 197 | up | left / 9–9 | left / 8–8 |
| `8fd97d0d…` / 357 | down | right / 8–8 | right / 9–9 |
| `ab3c8a6f…` / 326 | right | left / 10–10 | left / 8–9 |
| `be95288a…` / 308 | down | up / 9–9 | up / 9–9 |

These replay positions are diagnostics, not universal expected-move assertions.

## Predeclared gate

| Criterion | Result |
| --- | --- |
| zero search/audit errors | pass: default 0/0, candidate 0/0 |
| no increase in policy violations | pass: 0 vs 0 |
| no material structural-risk increase | pass: 0 vs 0 |
| non-inferior terminal/alive-at-cap survival | pass descriptively: terminal 89 vs 109; cap 0 vs 0 |
| positive paired signal with uncertainty | pass: Wilson lower bound 54.79%, p=0.0309 |
| candidate p99 ≤300 ms and ≤110% of default | fail: 12.841 ms and 110.37% |

Overall evidence decision: **inconclusive — do not promote**.

## Limitations

- One deterministic seed over generated Standard boards is not the live ladder distribution.
- Fixed depth 3 is reproducible but does not model production iterative-deepening depth distribution.
- Split pairs dominate (80/100); treating individual games as independent would be pseudo-replication.
- The Wilson interval is for decisive sweeps and excludes split/draw-containing pairs.
- Four targeted replay positions do not prove universal move optimality.

Raw evidence:

- `docs/evidence/issue-46-duel-weight-ab.json` — SHA-256 `ae9eb0c69d4b123aacf07ef4c4302717ea4448ab818b0ed169da599447876b3d`
- `docs/evidence/issue-46-duel-weight-replays.json` — SHA-256 `e2b888b2959f800dfd8915acc0e4fa9610ef0ecb63a6f3ecf78350d3aacb6205`
