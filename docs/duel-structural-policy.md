# Duel Structural Root Policy

The common minimax/root comparator is authoritative for duel root selection.
It orders search outcome and bound evidence, structural proof, forced-loss
survival, numeric intervals, and deterministic geometry before any corridor
proposal is considered. Structural evidence stays typed; it is not encoded as
a floating-point penalty.

## Corridor tie contract

A different corridor proposal may replace the comparator's incumbent only
when all of these conditions hold:

- both search records are exact and semantically equal in score, outcome,
  terminal distance, terminal cause, and bound;
- the complete structural records are semantically equal in `trap_status`,
  `trap_horizon`, `structural_proof`, `proof_cutoff`, `proof_horizon`,
  `structural_capacity`, `opponent_closure_considered`, `post_move_length`,
  `relaxed_static_capacity`, and `refutation_status`;
- `CoreCompareRootCandidates` returns `EQUAL` with its serialized equal-result
  reason `not_compared`; and
- the proposal has strictly better board-derived corridor metrics, ordered by
  immediate exits descending, forced steps ascending, then reachable space
  descending.

`INCOMPARABLE` is not `EQUAL`. Equal floating-point scores alone do not permit
an override, including when their bounds or any structural field differ.
Accepted audit values must also be representable native diagnostics: enum
strings must come from the native serializers, structural integers must be
nonnegative C `int` values, terminal distance must fit `uint16_t`, and terminal
causes must be a canonical serializer-ordered list drawn from `wall`,
`self_body`, `other_body`, `head_to_head`, `starvation`, `hazard`,
`invalid_command`, and `opponent_eliminated`. Corridor metrics are nonnegative
C `int` values, with immediate exits limited to the zero-to-three neighbors
remaining after excluding the previous cell.

## Diagnostics and offline audit

The top-level `corridor_guard` diagnostics record contains `considered`,
`incumbent`, `proposal`, `comparison_ordering`, `comparison_reason`,
`exact_tie_permitted`, `applied`, and `decision`. Each candidate includes its
move, corridor metrics, structural proof, relaxed capacity, post-move length,
and minimax score/outcome/bound. Stable decisions are `not_considered`,
`same_as_incumbent`, `rejected_search_order`, and `applied_exact_tie`.

`tools/check_duel_structural_policy.py` independently accepts an applied
override only when the audit, selected move, root records, exact search
semantics, complete structural semantics, and corridor metric ordering agree.
Rejected, same-candidate, and not-considered records receive no exemption and
are checked under the ordinary root policy. A missing audit for
`selection_reason=corridor_guard`, an incoherent applied claim, or a
structurally dominated proposal is a comparator violation.

## Issue 44 re-audit observation

The fixed-depth audit of the five captured issue positions observed
`same_as_incumbent` at turns 290, 317, 187, and 284. Turn 424 observed
`rejected_search_order`: the common comparator retained the structurally safe
incumbent. These are observations of the captured diagnostics, not proof that
the historical replay outcomes were caused by the corridor decision.
