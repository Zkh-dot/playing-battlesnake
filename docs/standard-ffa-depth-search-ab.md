# Standard FFA Depth Search A/B

Issue: #24

## Implementation

`standard-v1` now runs a budgeted depth 2-3 check after the existing depth-1
root scoring completes. The deepening pass:

- searches only the top root candidates from depth-1 scoring;
- marks opponents within `deepening_interaction_radius` as active;
- keeps distant snakes frozen as static obstacles;
- rolls back to the depth-1 result if the deepening pass exceeds its reserved
  deadline;
- records active opponents, frozen opponents, terminal rate, node count, trap
  refusals, and frozen-interaction risk in decision telemetry.

The first shipped policy is conservative: active-opponent deep scores are
telemetry-only, and a candidate is score-demoted only when depth 3 proves a
forced solo/frozen-obstacle terminal trap across all root opponent branches.
That avoided arena placement regressions observed when any active-opponent
adversarial terminal line was allowed to demote a move.

## Curated Trap Suite

`tests/test_issue_24_standard_ffa_deepening.py` includes a hungry FFA corridor
fixture adapted from the #11 trap family:

- depth-1 scoring chooses `up` for food in a two-cell corridor;
- depth 3 proves `up` is forced terminal;
- the deepened decision chooses `right` instead;
- the test asserts 0 active opponents, three frozen obstacle snakes, and
  `refused_trap=true`.

The same suite covers the relevance filter and the timeout path that keeps the
depth-1 result.

## Arena A/B

Candidate:

```text
standard-v1 with configs/evaluation_weights/standard-ffa-v1-tuned.json
```

Baseline:

```text
standard-v1 with the same tuned theta plus deepening_enabled=0
```

Commands were run locally:

```bash
python3 tools/standard_ffa_arena.py --games 24 --max-turns 80 --seed 61000 --candidate-theta configs/evaluation_weights/standard-ffa-v1-tuned.json --baseline-theta /tmp/standard-ffa-v1-tuned-depth1.json --baseline-strategy standard-v1 --output /tmp/issue24-ab-61000-solo.json --summary-output /tmp/issue24-ab-61000-solo.txt
python3 tools/standard_ffa_arena.py --games 24 --max-turns 80 --seed 63000 --candidate-theta configs/evaluation_weights/standard-ffa-v1-tuned.json --baseline-theta /tmp/standard-ffa-v1-tuned-depth1.json --baseline-strategy standard-v1 --output /tmp/issue24-ab-63000-solo.json --summary-output /tmp/issue24-ab-63000-solo.txt
python3 tools/standard_ffa_arena.py --games 24 --max-turns 80 --seed 65000 --candidate-theta configs/evaluation_weights/standard-ffa-v1-tuned.json --baseline-theta /tmp/standard-ffa-v1-tuned-depth1.json --baseline-strategy standard-v1 --output /tmp/issue24-ab-65000-solo.json --summary-output /tmp/issue24-ab-65000-solo.txt
```

Aggregate over 72 paired games:

```json
{
  "avg_baseline_objective": 0.7320572916666667,
  "avg_baseline_placement_score": 0.5965277777777778,
  "avg_candidate_objective": 0.7320572916666667,
  "avg_candidate_placement_score": 0.5965277777777778,
  "avg_objective_delta": 0.0,
  "avg_placement_delta": 0.0,
  "avg_score_delta": 0.0,
  "baseline_deaths": {
    "alive": 48,
    "unknown": 1,
    "won": 23
  },
  "baseline_fallback_count": 3,
  "baseline_latency_p95_max": 1.3172640010452596,
  "candidate_deaths": {
    "alive": 48,
    "unknown": 1,
    "won": 23
  },
  "candidate_fallback_count": 3,
  "candidate_latency_p95_max": 60.088953001468326,
  "deepening_completed": 4260,
  "deepening_refused_traps": 3,
  "deepening_timeout_depth1_fallbacks": 971,
  "frozen_interaction_checks": 10619,
  "frozen_interaction_risk_rate": 0.0,
  "frozen_interaction_risks": 0,
  "games": 72
}
```

## Verdict

Go for the conservative depth-search guardrail.

Placement, objective, death labels, and full strategy fallback counts matched
the depth-1 baseline across the 72-game local arena run. Candidate latency p95
remained under the 80 ms gate, with timeout cases falling back to the completed
depth-1 result rather than a partial deeper result.

The active-opponent search remains instrumented, but it is not allowed to
demote root moves yet. Earlier local trials that demoted any active-opponent
terminal line caused placement regressions, so active-opponent scoring should
graduate only after a separate A/B shows no placement loss.
