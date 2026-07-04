# Position Evaluation Engine

The position evaluation engine is a C-only duel evaluator for two-snake
Battlesnake positions. It is implemented in
`battlesnake/c-core/core/position_eval.c` and exposed through
`battlesnake/c-core/core/position_eval.h`.

The primary result fields are:

- `first_win_probability`: estimated probability that `first_snake_id` wins
  from the current board.
- `confidence`: the amount of the backed-up estimate that came from fully
  terminal-solved branches.
- `first_move_probabilities` and `second_move_probabilities`: root policy
  probabilities indexed by `MoveDirection` (`up`, `down`, `left`, `right`).

Terminal leaves have confidence `1.0`. Heuristic leaves created by depth limit
or timeout have confidence `0.0`. Internal nodes combine child confidence with
the same decision rule used for win probability, so the value stays in `[0, 1]`
and means "how resolved this estimate is under the configured search budget".

## Decision Modes

The default mode is `CORE_POSITION_DECISION_MATRIX`. It treats each Battlesnake
turn as a simultaneous zero-sum move matrix:

- rows are command directions for `first_snake_id`;
- columns are command directions for `second_snake_id`;
- each cell is the child `P(first snake wins)`.

For an alive snake, the engine includes all four Battlesnake command directions
in the matrix. It does not pre-filter with `BoardSafeMoves`: unsafe, suicidal,
and head-to-head-risky commands stay in the matrix, and `BoardCloneAndApply`
resolves the resulting deaths for each move pair.

The matrix solver attempts exact mixed strategies for matrices up to 4x4 using
small support enumeration. Degenerate cases that cannot be solved cleanly fall
back to pure maximin backup.

`CORE_POSITION_DECISION_PURE_MINIMAX` is a comparison mode. It uses conservative
pure maximin backup and is mainly useful when comparing this evaluator against
the existing move search.

## Current Limitations

The evaluator intentionally expands all four command directions for each live
snake instead of filtering with `BoardSafeMoves`. This preserves simultaneous
move semantics: a command that is unsafe against one opponent response may still
be useful against another. Fully trapped positions can therefore spend work on
children that immediately become terminal leaves.

The evaluator also does not reuse the existing `transposition_table.c`
infrastructure yet. At depth 2 and above, identical board states may be reached
through different move-pair orders and evaluated more than once. This is a
performance limitation, not a correctness limitation.

## Heuristic Leaves

When the engine reaches `max_depth` or exhausts `time_budget_ms`, it evaluates
the current board with the existing C board evaluator for each snake and
converts the score gap into probability:

```text
P(first wins) = 1 / (1 + exp(-(score(first) - score(second)) / 250))
```

The `250` scale is intentionally simple. It should be calibrated later against
exported replay outcomes or self-play rollouts. Calibration should change this
scale or the evaluation weights, not the recursion contract.

If the heuristic score produces a non-finite value, the probability is clamped
to `0.5`.

## Timeout Semantics

The top-level evaluator uses iterative deepening over root depths. For
`max_depth > 0`, it first evaluates a full root move matrix at depth 1, then
tries depth 2, depth 3, and so on until `max_depth` or the time budget stops it.

Root policy is only published from the deepest fully completed root matrix. If
the engine times out while computing depth `N`, all root values and root policy
from that incomplete depth are discarded, and the result falls back to the last
complete depth `< N`.

If the engine cannot complete depth 1, it returns the depth 0 heuristic value
for the current board. In that case root move policy arrays remain all zero
because no complete root matrix exists.

Inside non-root recursive nodes, timeout still fills only the missing matrix
cells with a heuristic fallback and preserves already-evaluated cells, exactly as
before. Any such fallback inside a depth attempt marks that whole root depth as
incomplete, so it keeps already completed shallower root depths useful while
allowing deeper attempts to stop quickly when the budget is exhausted.

## Diagnostics

`CorePositionEvalResult` also reports counters for calibration and tests:

- `nodes`: recursive nodes visited.
- `terminal_leaves`: leaves where a snake was already dead or missing.
- `heuristic_leaves`: leaves evaluated by the heuristic fallback.
- `timeout_leaves`: heuristic leaves caused by timeout.
- `expanded_children`: child boards evaluated after a legal move pair.
- `completed_depth`: deepest root depth whose full root matrix completed without
  timeout.
- `max_depth_started`: deepest root depth attempt that started before the
  evaluator stopped.
- `timed_out`: whether any part of the search hit the configured time budget.
- `elapsed_ms`: wall-clock time spent by the evaluator.
- `first_move_probabilities` / `second_move_probabilities`: root policy for
  move analysis and replay agreement metrics.
