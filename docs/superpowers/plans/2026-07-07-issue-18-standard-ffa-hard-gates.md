# Issue #18: Standard FFA Hard Gates And Candidate Classification

## Scope

Implement the deterministic safety layer for the Python Standard FFA dev strategy. This issue does not implement weighted scoring, opponent priors, or model integration.

## Files

- Add `battlesnake/strategies/standard_gates.py`.
- Add `tests/test_issue_18_standard_gates.py`.
- Leave `StrategyStandard` itself unchanged unless tests prove a small import/export hook is needed.

## API

- `classify_standard_ffa_candidates(board, snake_id) -> StandardGateResult`
  - Always returns four candidates in deterministic move order: `up`, `down`, `left`, `right`.
  - Exposes `eligible_candidates` for moves that pass terminal and severe hard gates.
  - Exposes `least_bad_candidate` for fallback when no candidate is eligible.
- `StandardMoveCandidate`
  - Stores primitive, serializable classification fields:
    `move`, `target`, `in_bounds`, `safe_by_board_rules`, `enters_hazard`,
    `candidate_food`, `candidate_occupied`, `immediate_safe_move_count_after`,
    `immediate_reachable_space`, `death_class`, `terminal`, `severe`,
    `eligible`, and `fallback_rank`.
  - Provides `to_dict()` for decision telemetry.

## Classification Rules

- Wall: target out of bounds, terminal.
- Self/body: occupied target that native board rules do not mark safe, terminal, but respect native tail-vacation semantics by trusting `board.is_safe`.
- Losing head-to-head: target cell is reachable by an equal-or-longer opponent head and native board rules mark it unsafe, terminal.
- Hazard starvation: target hazard would leave health at or below zero after move cost plus hazard damage, terminal.
- Trapped next turn: zero reachable space or zero next safe moves after the candidate, severe but not terminal.
- Eligible moves must have no terminal or severe hard gate.
- Fallback ranks by death-class severity, then higher reachable space, higher next safe-move count, then deterministic move order.

## Tests

- One curated board per death class:
  - wall;
  - body;
  - self;
  - head-to-head losing;
  - hazard starvation;
  - trapped next turn.
- Corner cases:
  - tail-vacation chase remains eligible when native rules prove the cell is safe;
  - equal-length head-to-head is classified;
  - food on a trapped cell records `candidate_food` while retaining trap classification.
- Result contract:
  - always four classified candidates;
  - eligible subset is deterministic;
  - candidate records serialize through `json.dumps(candidate.to_dict())`;
  - least-bad fallback prefers lower death severity, then larger reachable space.

## Verification

- `python3 -m pytest tests/test_issue_18_standard_gates.py`
- `python3 -m pytest tests/test_issue_17_ffa_native_primitives.py tests/test_issue_18_standard_gates.py tests/test_main_strategy.py`
