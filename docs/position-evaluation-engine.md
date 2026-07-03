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

Terminal leaves have confidence `1.0`. Heuristic leaves created by depth limit
or timeout have confidence `0.0`. Internal nodes combine child confidence with
the same decision rule used for win probability, so the value stays in `[0, 1]`
and means "how resolved this estimate is under the configured search budget".

## Decision Modes

The default mode is `CORE_POSITION_DECISION_MATRIX`. It treats each Battlesnake
turn as a simultaneous zero-sum move matrix:

- rows are legal moves for `first_snake_id`;
- columns are legal moves for `second_snake_id`;
- each cell is the child `P(first snake wins)`.

The first implementation attempts exact mixed 2x2 matrix games when the mixed
strategy is well-defined. Degenerate 2x2 matrices and all larger legal-move
matrices fall back to pure maximin backup. Exact 3x3 and 4x4 support is the next
mathematical improvement.

`CORE_POSITION_DECISION_PURE_MINIMAX` is a comparison mode. It uses conservative
pure maximin backup and is mainly useful when comparing this evaluator against
the existing move search.

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

The evaluator resolves terminal leaves and depth-limit leaves before checking
the deadline for expansion. It then checks the deadline before expanding a node
and before each child. If the budget expires inside a simultaneous move matrix,
already evaluated cells are preserved and only missing cells are filled.

Missing cells use one heuristic probability computed from the current parent
board, not per-move child-board heuristic evaluation. Those fallback cells have
confidence `0.0` and count as both heuristic and timeout leaves.

This keeps the result usable under tight budgets: partial exact work is retained
instead of discarding the whole node.

## Diagnostics

`CorePositionEvalResult` also reports counters for calibration and tests:

- `nodes`: recursive nodes visited.
- `terminal_leaves`: leaves where a snake was already dead or missing.
- `heuristic_leaves`: leaves evaluated by the heuristic fallback.
- `timeout_leaves`: heuristic leaves caused by timeout.
- `expanded_children`: child boards evaluated after a legal move pair.
- `timed_out`: whether any part of the search hit the configured time budget.
- `elapsed_ms`: wall-clock time spent by the evaluator.
