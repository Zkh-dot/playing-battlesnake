# Issue #19: Standard FFA V1 Deterministic Scoring

## Scope

Implement the first complete Python `StrategyStandard.decide` for the dev snake. Keep it model-free and deterministic. Reuse issue #18 hard gates and native primitives. Do not port this to the native server in this issue.

## Files

- Update `battlesnake/strategies/standard.py`.
- Update `battlesnake/main.py` to register an opt-in Standard FFA variant.
- Add `tests/test_issue_19_standard_strategy.py`.
- Add `docs/standard-ffa-v1-smoke.md` after running the local smoke/latency command.

## Implementation

- Add a serializable `DEFAULT_STANDARD_THETA` dict in `standard.py`.
  - Include native evaluator overrides: `territory_delta`, `opponent_safe_moves`, `opponent_reachable_space`.
  - Include scoring weights for space, escape, food, contested food, head pressure, pocket, expected, and worst-case aggregation.
  - Keep hard-gate severity outside theta.
- `StrategyStandard.decide(board, snake_id)`:
  - enforce a local 80 ms default internal deadline and return first-safe fallback on timeout;
  - call `classify_standard_ffa_candidates`;
  - return `least_bad_candidate` when no candidates are eligible;
  - build deterministic opponent priors from uniform safe moves, with bookkeeping-uniform over all moves when trapped;
  - build a bounded scenario set per candidate: top probability joint responses, forced equal/longer head-to-head danger, and one worst-case nearby response per opponent;
  - `clone_and_apply` each scenario and score surviving boards with native `evaluate` plus v1 adjustments from the spec;
  - aggregate as `hard_gate + w_expected * expected + w_worst * worst`;
  - deterministic tie-break by move order.

## Tests

- Scenario-suite qualitative tests:
  - avoids wall;
  - prefers larger space;
  - refuses pocket with food;
  - takes safe food when hungry;
  - avoids contested equal-length food;
  - refuses suicidal head-to-head.
- Contract tests:
  - theta is JSON-serializable;
  - decide returns a valid move on 4-snake boards;
  - the Standard variant is registered in `battlesnake.main`.

## Verification

- `python3 -m pytest tests/test_issue_19_standard_strategy.py`
- `python3 -m pytest tests/test_issue_18_standard_gates.py tests/test_issue_19_standard_strategy.py tests/test_main_strategy.py`
- Local smoke/latency command recorded in `docs/standard-ffa-v1-smoke.md`.
