# Issue #24: Standard FFA relevance-pruned depth search

## Plan

1. Keep the current depth-1 Standard FFA root scoring as the authoritative
   baseline.
2. Add a budgeted all-or-nothing deepening pass for the top depth-1 root
   candidates.
3. Use a relevance filter: nearby opponent heads are active, distant snakes are
   frozen as static obstacles.
4. Treat timeout as a depth-1 fallback, not as a partial deeper result.
5. Add a curated FFA corridor trap fixture where depth-1 picks the fatal move
   and depth 3 refuses it.
6. Validate locally with paired arena runs against depth-1-only.

## Result

Implemented in `battlesnake/strategies/standard.py`.

The deepening pass searches depth 3 by default, reserves 20 ms for request
deadline safety, and records detailed telemetry. To avoid the placement
regression observed in local A/B, this first version demotes a move only when a
forced solo/frozen-obstacle trap is proven. Active-opponent deep search is
computed and logged, but does not yet change root ordering.

Arena telemetry was extended in `tools/standard_ffa_arena.py` to report:

- full strategy fallback count;
- completed deepening decisions;
- depth-1 timeout fallbacks;
- trap refusals;
- frozen-interaction checks and risk rate.

## Validation

Focused tests:

```bash
python3 -m pytest tests/test_issue_24_standard_ffa_deepening.py
```

Regression tests:

```bash
python3 -m pytest tests/test_issue_19_standard_strategy.py tests/test_issue_21_standard_ffa_arena.py tests/test_issue_23_lightgbm_prior.py
```

Arena A/B:

- 72 paired local games over seeds 61000, 63000, 65000;
- candidate objective 0.7320572916666667;
- baseline objective 0.7320572916666667;
- objective delta 0.0;
- placement-score delta 0.0;
- candidate fallback count 3;
- baseline fallback count 3;
- candidate max p95 latency 60.088953001468326 ms;
- 3 forced trap refusals;
- frozen-interaction risk rate 0.0.

Detailed report: `docs/standard-ffa-depth-search-ab.md`.
