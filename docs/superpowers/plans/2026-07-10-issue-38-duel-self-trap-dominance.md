# Issue #38: Duel Self-Trap Dominance Specification

## Decision Summary

For [issue #38](https://github.com/Zkh-dot/playing-battlesnake/issues/38), do
not implement the proposed `reachable_cells < own_length + growth_buffer` gate.
Static capacity is useful evidence, but it is not a proof that a snake is
trapped, and an arbitrary growth buffer would encode replay-specific tuning.

Implement a root eligibility layer for Standard duels instead. It has two
narrow dominance rules:

1. Reject a command with no surviving reply when another command has at least
   one, except when the first command guarantees a draw.
2. Reject a proven short-horizon self-trap only when another command is a real
   structural opportunity: it has a surviving reply, is not proven
   trapped, and has enough relaxed static capacity for its body.

The remaining commands are still selected by the existing strict maximin
search. The new layer is a policy about which risks are worth considering, not
a replacement evaluator or an expected-value opponent model.

Issue [#32](https://github.com/Zkh-dot/playing-battlesnake/issues/32) is phase
zero of this work. Terminality, draw state, distance, cause, and alpha-beta
bound kind must be explicit search metadata before issue #38 adds more root
policy. Numeric score bands must not stand in for facts about the search tree.

## Problem And Evidence

The two issue #38 positions are different failure classes.

| Position | Verified behavior on `1975fac` | Interpretation |
| --- | --- | --- |
| `cb52...` T209 | `up` guarantees roughly 11 joint turns; `down` can be refuted in roughly 3 | `up` is strict-maximin rational. Selecting `down` is an intentional ladder-risk policy because `up` is a proven self-trap and `down` reaches a large open region. |
| `970...` T586 | every root score is `-999000`; `down` keeps us alive against 0 modeled replies while `right` keeps us alive against 2 of 3 | This is a root eligibility and fallback defect. A guaranteed self-collision must not beat a contingent head-to-head risk. |
| issue #30 `978a...` T407 | `up` loses later than `left`; both alternatives have static capacity 4 for post-move lengths 38 and 39 | The existing later-loss ordering is correct here. There is no large-region witness, so issue #38 must not override it. |

The exported T209 payload contains food at `(1,10)` and `(2,10)`, not at
`(0,10)` as stated in the issue. An exact ordered-body rollout still proves the
top-row trap. The implementation must consume fixture data rather than copy
coordinates from the issue prose.

`CoreSpaceTimeCompute()` is not a trap prover. On T209 after `up`, it reports a
large time-expanded reachable set, tail reachability, and `dead=false`. Its
state is a point `(cell, time)`; it does not carry the ordered body created by a
path or food-induced growth. Coordinate reuse can therefore look feasible even
when no legal snake rollout exists.

The proposed narrow predicates were also checked against all four current
issue #30 fixtures:

| Fixture | Alive replies per candidate | Tail-blocked capacities | Post-move lengths |
| --- | --- | --- | --- |
| `257c...` T481 | `3/3`, `3/3` | `85`, `85` | `37`, `37` |
| `e1ca...` T421 | `3/3`, `3/3` | `73`, `78` | `45`, `44` |
| `978a...` T407 | `3/3`, `3/3` | `4`, `4` | `38`, `39` |
| `cf5d...` T360 | `3/3`, `3/3` | `87`, `87` | `34`, `34` |

Rule A changes none of them. T407 has no full-body-capacity alternative, and
the other fixtures have no candidate with capacity below its body length, so
Rule B also leaves their current root sets unchanged.

## Goals

- Select `down` at T209 under production budgets, while documenting that it is
  not maximin-safe.
- Select `right` at T586 instead of an immediate self-collision.
- Preserve the forced-loss survival ordering introduced for issue #30.
- Give native search, native server routing, Python fallbacks, and diagnostics
  one explainable root command classification.
- Make every exclusion proof-based or exact one-ply classification. An
  incomplete analysis must leave a command eligible.
- Keep the root work bounded and small relative to the search budget.

## Non-Goals

- No game-id, turn-number, edge, or top-row special cases.
- No global search-depth or timeout increase.
- No probability estimate from reply counts; `2/3` is not ranked above `1/3`.
- No global expected-value or learned-opponent selection policy.
- No arbitrary collision-severity score table or growth buffer.
- No use of `CoreSpaceTimeCompute.dead` as proof.
- No replacement of `CoreMinimaxMove` with `CorePositionEvaluateDuel`.
- No extension of this policy to FFA, constrictor, wrapped, or royale boards in
  this issue.
- No claim that T209 `down` is safe or maximin-optimal.

## Policy Activation

Expose two root modes rather than silently redefining minimax:

- `STRICT_MINIMAX` retains the current root move generator and bypasses issue
  #38 dominance. It is used as a control in diagnostics and tests.
- `STANDARD_LADDER_OPPORTUNITY` enables the issue #38 eligibility rules and is
  the production mode for Standard duels. It profiles all four own commands;
  deterministic deaths are handled by Rule A rather than hidden inside
  `BoardSafeMoves`.

Other rulesets remain on their existing behavior. Diagnostics must accept or
report the mode so T209 can show both facts: strict minimax selects `up`, while
the ladder-opportunity policy excludes `up` and selects `down`.

Add the following public configuration contract:

```c
typedef enum {
    CORE_ROOT_POLICY_STRICT_MINIMAX = 0,
    CORE_ROOT_POLICY_STANDARD_LADDER_OPPORTUNITY = 1,
} CoreRootPolicy;

/* New field in CoreSearchConfig. */
CoreRootPolicy root_policy;
```

`CoreSearchConfigDefault()` and `CoreMinimaxMove()` use
`STANDARD_LADDER_OPPORTUNITY`; the policy activates only when
`ruleset_name == "standard"` and `snake_count == 2`. Explicitly zero-initialized
legacy configs remain strict because enum value zero is `STRICT_MINIMAX`.

Python `minimax_move()` and `minimax_diagnostics()` accept
`root_policy="standard_ladder_opportunity"` by default and also accept
`"strict_minimax"`. The native server uses the default opportunity policy for
Standard duels. Diagnostics always return the effective policy name.

## Phase Zero: Tagged Search Values

Replace internal `double` returns with a value that keeps score and proof
metadata together. Names may follow local conventions, but the contract is:

```c
typedef struct {
    double score;
    CoreOutcome outcome;          /* WIN, DRAW, UNRESOLVED, LOSS */
    uint16_t terminal_distance;   /* node-relative; undefined if unresolved */
    CoreTerminalCause cause;
    CoreValueBound bound;         /* EXACT, LOWER, UPPER */
} CoreSearchValue;
```

Required semantics:

- A depth/timeout/heuristic leaf is `UNRESOLVED`, regardless of its numeric
  score.
- A mutual death is `DRAW`. The current order that checks our absence first
  must not collapse mutual death into `LOSS`.
- Use an explicit draw utility, initially the midpoint between configured
  terminal win and loss values. The outcome tag, not coincidence with that
  number, identifies the result as a draw.
- Terminal survival shaping is applied only when `outcome` is terminal. Delete
  score-band inference from `core_score_is_terminal_*()` and
  `core_adjust_terminal_child_score()`.
- Preserve the current default ordering by deriving numeric terminal scores as
  `terminal_win - step * distance` for `WIN` and
  `terminal_loss + step * distance` for `LOSS`. Do not distance-shape `DRAW`.
- Keep current numeric comparison behavior and public weight semantics. The
  metadata determines whether terminal-distance shaping is legal; it does not
  silently redefine all weight overrides.
- Terminal distance is relative to the node. Recursive backup increments it by
  one. A transposition-table entry must not contain root-relative ply.
- Transposition-table store/probe carries the complete value and bound kind.
  If that cannot be done in the first patch, metadata-dependent entries must
  bypass the table rather than reconstruct metadata from `score`.
- A fail-low root result is an `UPPER` bound, not an exact score. A full-window
  re-search is required before any later policy treats it as exact.
- `CoreSpaceTimeCompute.dead` and other structural heuristics remain
  `UNRESOLVED`; only canonical terminal transition results and explicit proof
  procedures may emit terminal outcomes.
- The root selected move, selected value, reported score, and reported bound
  must always describe the same branch, including timeout and corridor-guard
  paths.

A shared terminal classifier should be used by minimax and
`position_eval.c`. It must classify both snakes before assigning `WIN`, `DRAW`,
or `LOSS` from the requested snake's perspective.

## Canonical Root Reply Profile

Build one profile for all four commands that an endpoint can return. Do not use
`BoardSafeMoves` as the source of truth: it intentionally combines contingent
head-to-head danger with deterministic wall/body death. It may still provide
ordering advice.

For each own command:

1. Enumerate every opponent command returned by `core_command_moves()`, the
   production duel command model. Preserve its non-neck-reverse rule and
   length-two vacating-tail exception.
2. Apply the command pair simultaneously through the canonical board
   transition implementation.
3. Classify the child as `WIN`, `DRAW`, `BOTH_ALIVE`, or `LOSS` from our
   perspective and record collision-cause bits.
4. Record reply masks and counts. Internally these can be four-bit masks;
   diagnostics expose move names as well.

Do not duplicate wall, body, tail-vacation, growth, head-to-head, hazard, or
starvation rules in a root-only classifier. Extend the canonical transition to
emit causes if necessary.

Define `survive_mask = win_mask | both_alive_mask`. A command has
`no_surviving_reply` when this mask is empty. Mutual death remains a draw, so a
command whose every reply is `DRAW` is a protected guaranteed draw. A command
with no surviving reply and at least one `LOSS` is eligible for Rule A even if
another opponent command would also crash and produce `DRAW`.

Reply counts are used only for the zero-versus-positive opportunity test; they
are not opponent probabilities.

Expose this as one fixed-size native primitive:

```c
CoreStatus CoreDuelRootProfile(
    const Board* board,
    const char* snake_id,
    CoreDuelRootProfileResult* out_result
);
```

The result owns no heap memory. Native minimax and the C server call it
directly. The Python extension exposes the same data as
`duel_root_profile(board, snake_id)`, and on two-snake boards all Python
no-safe-move fallbacks call that binding instead of reimplementing collision
rules. `CoreDuelRootProfile` rejects boards with a snake count other than two
with an explicit status; it is not defined for FFA boards.

There is no supported pure-Python board runtime to preserve: `game.py` already
imports the C-backed `Board`. If the native extension cannot load, application
startup fails as it does today. If a caller supplies an invalid board, surface
the profile error; do not turn an analysis failure into a hardcoded `up` move.

## Opponent-Relaxed Self-Trap Proof

Run this analysis once per root command and only for Standard duels. It is an
exact ordered-body simulation in a world relaxed in our favor:

- remove opponents and their bodies;
- make hazards harmless and ignore health/starvation;
- apply food on the root destination, growth, and tail freezing exactly;
- after the root transition, remove remaining food and spawn no future food;
- apply the root command and every continuation to the full ordered body.

Removing non-root food is deliberate: food is shared state, and an opponent
could consume it before our forced line arrives. In the health-free model,
removing it maximizes future tail mobility. A trap that remains is therefore
valid regardless of who consumes later food. A separate known-food rollout may
be logged to explain harmful growth, but it is not the dominance proof.

Apply the root command first. Death on that transition is `IMMEDIATE_DEATH`;
Rule A, not Rule B, owns any exclusion. If it survives, enumerate commands that
leave the snake alive after each subsequent transition:

- zero continuations: `PROVEN_SELF_TRAP`;
- one continuation: apply it and continue;
- two or more continuations: `OPEN_BRANCH`, which is not a proof of safety;
- a repeated full ordered-body state: `SURVIVES_CYCLE`;
- the proof horizon is reached with the snake alive: `SURVIVES_HORIZON`;
- deadline, allocation failure, or an external work cap reached before the
  proof horizon: `UNKNOWN`.

`trap_horizon` is the number of successful transitions including the root
command. A structural exclusion may use the proof only when
`trap_horizon < post_move_length`. This identifies a chamber too short for the
body to turn over without converting every eventual forced loss into a root
ban. `IMMEDIATE_DEATH` therefore reports horizon zero.

The normal proof horizon is `post_move_length - 1`. Reaching it alive is
`SURVIVES_HORIZON`, not a trap. Deterministic state storage must cover that
horizon; an earlier deadline, configured cap, or allocation interruption is
`UNKNOWN`.

Removing the opponent makes a self-trap proof conservative, but it does not
prove that we fail to win or draw first. A single opponent witness line is not
enough: a different future own command might still force a draw.

Before Rule B excludes a candidate, run a bounded exact outcome-only search on
the original duel with that root command fixed. Search through
`trap_horizon + 1` joint transitions using production's pure maximin nesting.
For a stronger refutation than the normal safety-filtered search, maximize over
all four future own protocol commands and minimize over every
`core_command_moves()` opponent reply. Canonical terminal transitions return
tagged `WIN`, `DRAW`, or `LOSS`; a nonterminal horizon leaf is `UNKNOWN`. The
result is `PROVEN_REFUTABLE` only when the backed-up root result is exact
`LOSS`. `DRAW`, `WIN`, or `UNKNOWN` cannot authorize exclusion.

The refutation solver may short-circuit an opponent reply set after finding an
exact `LOSS`, but a future-own-move node returns `LOSS` only when every own
command has such a refutation. `UNKNOWN` is never coerced to `LOSS`.

This proof means that for every future own continuation, the opponent model has
a reply that preserves the loss refutation. If its node/deadline cap is reached,
the structural result is `UNKNOWN` for dominance purposes. The self-trap proof
remains root policy evidence and must never be inserted into recursive search
as a terminal `LOSS`.

## Relaxed Static Capacity

Compute a separate `relaxed_static_capacity` diagnostic on the relaxed
post-move board. This requires a dedicated flood fill; do not call
`CoreReachableSpace()`, which unblocks a tail that it expects to vacate.

For this metric, mark every post-move own-body coordinate, including the tail,
as blocked, then unblock the head as the flood-fill start. Count the head and
every connected empty cell. The root transition has already removed the old
tail when the root move did not eat, so no additional current or future tail
release is modeled. Opponent bodies and hazards remain removed.

Capacity is an eligibility witness, never a trap proof:

- `capacity < post_move_length` supports a proven short-chamber diagnosis;
- `capacity >= post_move_length` establishes that an alternative at least has
  enough static room to contain its current body;
- future tail motion can make a low static capacity survivable, so capacity
  alone must never exclude a command.

Do not add a growth buffer. Mandatory root growth is already handled exactly by
the ordered-body simulation and post-move length. Later known-food effects may
be diagnostic, but the dominance proof uses the more mobile no-food relaxation.

The issue #30 table above was recomputed with this tail-blocked definition. The
three large-capacity fixtures remain well above body length and both T407
alternatives remain well below it.

## Dominance And Selection Rules

Start with all four root commands and build an `allowed_root_mask` before
minimax.

### Rule A: No Surviving Reply

If at least one candidate has an immediate `WIN` or `BOTH_ALIVE` reply, exclude
a candidate when it has no such surviving reply and has at least one `LOSS`
reply. Do not exclude a candidate whose replies are all `DRAW`.

This is the T586 and issue #39 rule. It does not compare positive reply counts
and it does not claim the surviving command is safe.

### Rule B: Proven Structural Self-Trap

An allowed candidate is structurally dominated only when all of these hold:

- `trap_status == PROVEN_SELF_TRAP`;
- `trap_horizon < post_move_length`;
- `relaxed_static_capacity < post_move_length`;
- `refutation_status == PROVEN_REFUTABLE` from the exact bounded outcome
  search;
- another allowed candidate has at least one reply where we remain alive;
- that alternative is `OPEN_BRANCH`, `SURVIVES_CYCLE`, or
  `SURVIVES_HORIZON`, not `PROVEN_SELF_TRAP` or `UNKNOWN`; and
- the alternative's
  `relaxed_static_capacity >= alternative_post_move_length`.

`OPEN_BRANCH`, `SURVIVES_CYCLE`, `SURVIVES_HORIZON`, and `UNKNOWN` are all
non-excludable as candidates. `UNKNOWN` cannot serve as the structural witness
for excluding another command because incomplete analysis must fail open.

If no candidate has a surviving reply, all candidates are proven trapped,
every alternative has insufficient capacity, or analysis is incomplete, keep
the relevant candidates and use tagged maximin's current terminal-distance
ordering. There must always be at least one allowed command.

### Search And Fallback

- Search only allowed root commands; recursive move generation remains
  unchanged.
- Use existing strict maximin, iterative deepening, and later-loss ordering
  among allowed commands.
- On timeout, use the last completed depth within the same allowed mask.
- If no depth completes, the deterministic fallback must rank only allowed
  commands and must never default blindly to `up`.
- The issue #33/#36 corridor guard may remain temporarily for regression
  parity, but it must operate inside the allowed mask and must not consume an
  inexact root bound as an exact score.
- `StrategyFirstSafe`, `StrategyDuel._fallback_move`, and
  `battlesnake.main.fallback_move` must call the public native immediate profile
  when `safe_moves()` is empty and the board is a two-snake board. On that duel
  path they must not duplicate collision rules or retain a hardcoded `up`
  fallback. `StrategyFirstSafe` is also the Standard FFA fallback
  (`StrategyStandard._fallback`), so on boards the duel profile rejects, all
  three fallbacks keep their current behavior, including the existing `up`
  default; changing non-duel fallback policy is outside this issue.

## Diagnostics Contract

Preserve `root_move_scores` for compatibility, but do not imply that every
entry is exact. Add structured diagnostics for all four directions:

```text
root_candidates: {
  up: {
    evaluated, allowed, rejection_reason, safe_by_board_rules,
    reply_outcomes, alive_reply_mask, alive_reply_count,
    draw_reply_mask, immediate_causes,
    trap_status, trap_horizon, relaxed_static_capacity,
    refutation_status,
    minimax_score, minimax_outcome, minimax_terminal_distance,
    minimax_cause, minimax_bound
  }
}
root_allowed_mask
root_policy_applied
selection_reason
root_analysis_nodes
root_analysis_elapsed_ms
```

Masked or unevaluated commands use `null` minimax fields. `rejection_reason` is
one of `none`, `no_surviving_reply`, or `proven_short_self_trap`. Diagnostics
must distinguish analysis `UNKNOWN` from a negative proof result.
`refutation_status` is one of `proven_refutable`, `not_refutable`, `unknown`,
or `not_analyzed`; Rule B accepts only `proven_refutable`.

## Files

- `battlesnake/c-core/core/core_algorithms.c/.h`: tagged values, root profiles,
  self-trap proof, allowed mask, and selection.
- `battlesnake/c-core/core/search_stats.c/.h`: structured root diagnostics and
  selected-value coherence.
- `battlesnake/c-core/core/transposition_table.c/.h`: tagged values and bounds.
- `battlesnake/c-core/core/search_state.c/.h`: reuse exact make/unmake body
  transitions; do not create a second rules engine.
- `battlesnake/c-core/core/position_eval.c`: use the shared terminal classifier.
- `battlesnake/c-core/py-core/py_core.c` and
  `battlesnake/battlesnake_native.pyi`: diagnostics exposure.
- `battlesnake/c-core/server/battlesnake_strategy.c`: native routing parity.
- `battlesnake/strategies/first_safe.py`, `battlesnake/strategies/duel.py`, and
  `battlesnake/main.py`: least-bad fallback parity.
- Add `tests/fixtures/issue_38_dead_tunnel_positions.json` and
  `tests/test_issue_38_dead_tunnel.py`.
- Extend `tests/test_search_diagnostics.py`, `tests/test_issue_36_endgame.py`,
  `tests/test_main_strategy.py`, and `tests/c/test_battlesnake_strategy.c`
  where their contracts are affected.

## Acceptance Cases

| Case | Required classification and selection |
| --- | --- |
| issue #38 `cb52...` T209 | `up`: `PROVEN_SELF_TRAP`, horizon about 11, relaxed static capacity 11 for post-move length 24, and `PROVEN_REFUTABLE`. `down`: `OPEN_BRANCH` with relaxed static capacity at least its length. Strict mode selects `up`; opportunity mode excludes it and selects `down`. Tests must also state that `down` has a short adversarial refutation. |
| issue #38 `970...` T586 | `down`: alive against 0 modeled replies, self-collision cause, and trap status `IMMEDIATE_DEATH`. `right`: alive against 2 of 3. Exclude `down`; select `right`. |
| issue #39 T8 | Never select guaranteed wall/self collision when `left` or `down` has a contingent surviving reply. Check native and Python fallbacks. |
| issue #30 `978a...` T407 | Both relevant capacities are 4, below post-move lengths 38 and 39, so Rule B does not fire. Preserve `up` (`-995000`) over the earlier-loss `left` (`-996000`). |
| issues #33 and #36 | Existing fixture choices and move/score coherence remain unchanged. |
| valid tail cycle | A repeated full body state reports `SURVIVES_CYCLE` and is never excluded even if a static flood fill is small. |
| branch before trap | Reports `OPEN_BRANCH`; capacity may be logged but cannot turn it into a proof. |
| analysis cap/timeout | Reports `UNKNOWN`, preserves normal maximin selection, and never partially applies Rule B. |

Add unit coverage for:

- root-destination food freezing the tail and a paired no-food position;
- later food being removed from the dominance proof but retained in the
  optional known-food diagnostic;
- future food not spawning in proof search;
- equal-length head-to-head draw versus unequal-length loss;
- simultaneous mutual death;
- vacating-tail and growth transitions;
- the length-two reverse exception;
- hazard and starvation causes in the immediate profile;
- all four commands having no surviving reply, including mixtures of draw and
  loss outcomes and an all-draw protected case;
- a geometrically proven trap that is a forced win or draw is not excluded;
- an incomplete refutation search reports `UNKNOWN` and does not exclude;
- root-policy defaults and explicit strict/opportunity overrides across C,
  Python, and native server entry points;
- `CoreDuelRootProfile` and its Python binding return identical reply masks and
  causes;
- terminal metadata under narrow terminal weights and large positive/negative
  heuristic weights from issue #32;
- true terminal-distance ordering;
- transposition table on/off and clone versus make/unmake parity;
- fixed-depth determinism and `minimax_move`/diagnostics parity;
- `root_move_scores` bound labeling after a root fail-low cutoff.

## Performance Constraints

- Build the reply profile once: at most four own commands times the production
  opponent command count.
- Run root proof analysis once per request, not once per iterative-deepening
  depth.
- Invoke the exact refutation solver only after the trap, horizon, capacity,
  and alternative-witness predicates for Rule B already hold.
- Use deterministic state/node caps and the existing request deadline. A cap
  produces `UNKNOWN`; it must not consume the remaining search budget trying to
  manufacture a proof.
- Record analysis nodes and elapsed time separately from minimax nodes.
- Reproduce T209 and T586 at budgets `50`, `100`, `150`, `200`, and `300` ms.
- Preserve the issue #36 opt-in 300 ms depth gate: minimum completed depth at
  least 8 and median at least 10 on its fixture set.
- Benchmarks and replay reconstruction must be local/offline. Do not probe the
  live production snake port.

## Rejected Alternatives

- **Raw capacity gate:** tail motion makes static flood fill incomplete, and
  `capacity < length` is not a death proof.
- **`length + growth_buffer`:** the buffer is arbitrary and duplicates growth
  that exact body simulation can model.
- **Fixed move-severity weights:** they hide simultaneous reply semantics and
  become another threshold-tuning surface.
- **Blanket gamble-over-later-loss policy:** it would regress issue #30 and can
  throw away a genuine forced win or draw.
- **Deeper minimax only:** T209's desired move is worse under strict adversarial
  backup, so more depth reinforces `up` rather than selecting `down`.
- **`CoreSpaceTimeCompute.dead` as proof:** its point-agent state loses ordered
  body and growth history and is a verified false negative on T209.
- **Treating `root_move_scores` as exact:** alpha-beta cutoffs can leave
  fail-low upper bounds.
- **Switching to the position-evaluation engine:** mixed-strategy or
  expected-value selection is a separate policy change with a much larger
  regression surface.

## Verification

```bash
python3 setup.py build_ext --inplace --force
python3 -m pytest tests/test_issue_38_dead_tunnel.py tests/test_search_diagnostics.py tests/test_issue_36_endgame.py tests/test_main_strategy.py -q
tools/run_c_server_tests.sh
BATTLESNAKE_RUN_PERF_TESTS=1 python3 -m pytest tests/test_issue_36_endgame.py -q
```

The implementation is complete only when the structured diagnostics explain
why each replay move was eligible or excluded. Exact move assertions without
the proof/reply fields are insufficient because they can pass through an
unrelated tie-break.
