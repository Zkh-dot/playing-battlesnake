# Corridor Guard Ordering Contract Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Prevent the post-search corridor guard from replacing a better or structurally dominant minimax root while preserving its usefulness only for true semantic ties and making every proposal auditable.

**Architecture:** Keep `core_select_root_candidate` as the authoritative layered root ordering introduced by issue #42. Replace the regression-derived post-search thresholds with a general corridor proposal over allowed safe roots, then permit a different proposal only when `CoreCompareRootCandidates` reports semantic equality and the exact search records are equal; record the incumbent, proposal, metrics, comparison, predicate, and decision in native diagnostics whether the proposal is applied or rejected.

**Tech Stack:** C11 native search core, CPython extension bindings, Python replay fixtures and pytest/unittest regressions.

---

## Baseline and design evidence

- Fresh base: `origin/main` at `6cfabb3dec5cd3fe68240002582f59d16fcd7f25`.
- `1985bf57-7fb4-4842-8ac0-b8900d31e1be` T424 currently completes depth 11 with `up=1942.4` (`safe`, relaxed capacity 76) and `down=1943.8` (`unknown`, relaxed capacity 7 for length 39), but reports `down/corridor_guard`. The issue-42 structural relation makes `up` authoritative before the post-search overwrite.
- All five audit positions currently report `selection_reason=corridor_guard`. `7ea501e7` T187 is the equal-score case; T424 is structurally dominated. The #33 and #36 positive fixtures currently report `selection_reason=minimax`, proving the common root selector already preserves those cases without the legacy override.
- Rejected alternative 1: deleting all corridor proposal logic would stop the defect but would not provide the issue-required candidate/predicate diagnostics or preserve a principled true-tie extension point.
- Rejected alternative 2: retaining the current `length >= 12`, forced-step delta, and exit-count thresholds would encode historical fixtures rather than an ordering contract.
- Selected design: choose the lexicographically best corridor metrics without fixture thresholds, but gate a different post-search choice on exact semantic search equality plus `CoreCompareRootCandidates(...).ordering == EQUAL`. `INCOMPARABLE` is not a tie.

## Task 1: Add replay-derived fixtures and failing contract tests

**Files:**

- Create: `tools/build_issue_44_fixtures.py`
- Create: `tests/fixtures/issue_44_corridor_guard_positions.json`
- Create: `tests/test_issue_44_corridor_guard.py`
- Modify: `tests/test_search_diagnostics.py:12-42`

1. Add a deterministic fixture builder modeled on `tools/build_issue_41_fixtures.py`. Read the five checked-in/raw replay exports by game ID and turn, identify the live `scvnak`, convert engine coordinates and ruleset fields, and emit only board state plus evidence (`game_id`, `turn`, historical guard move, expected authoritative move where specified). Do not encode replay IDs in production logic.
2. Generate and inspect `tests/fixtures/issue_44_corridor_guard_positions.json` for:
   - `091dc137...` T290 (`right`, alternative `left`),
   - `1985bf57...` T424 (reject `down`, choose `up`),
   - `74f38216...` T317 (`left`, alternative `right`),
   - `7ea501e7...` T187 (reject the equal-score `right` override in favor of the common comparator),
   - `f5d7c374...` T284 (`up`, alternative `down`).
3. Add a fixture-builder unit test using a temporary export directory so schema/conversion failures are isolated from ignored local replay files.
4. Add focused fixed-depth tests which assert:
   - T424 selects `up`, never `down`, at production-like fixed depth 11;
   - the selected T424 candidate is structurally `safe`, while `down` is capacity-deficient and not allowed to override it;
   - all five positions expose a complete corridor audit with incumbent/proposal metrics, structural proof, minimax score/outcome/bound, comparison ordering/reason, exact-tie predicate, applied flag, and decision;
   - a different proposal is never applied unless the exact-tie predicate is true;
   - T187 records an explicit rejected proposal rather than treating equal floating scores as sufficient;
   - diagnostics remain coherent: top-level move and score equal the applied candidate and root score.
5. Extend `EXPECTED_DIAGNOSTIC_KEYS` for the new top-level `corridor_guard` record.
6. Run `python3 -m pytest -q tests/test_issue_44_corridor_guard.py tests/test_search_diagnostics.py -k 'issue_44 or minimax_diagnostics_shape'`. Expected: fixture-builder test passes; behavioral/diagnostic tests fail because current main selects `down` at T424 and has no audit record.
7. Commit: `test: reproduce corridor guard ordering violations`.

Acceptance coverage: T424 production-like regression; all five audit inputs; failing evidence for diagnostic contract and forbidden exact/structural override.

## Task 2: Enforce the semantic tie-only guard and expose native diagnostics

**Files:**

- Modify: `battlesnake/c-core/core/search_stats.h:55-177`
- Modify: `battlesnake/c-core/core/search_stats.c:1-70`
- Modify: `battlesnake/c-core/core/core_algorithms.c:4360-4527, 6240-6350`
- Modify: `battlesnake/c-core/py-core/py_core.c:500-890`
- Modify: `battlesnake/battlesnake_native.pyi:65-130`
- Modify: `tests/test_issue_44_corridor_guard.py`
- Modify: `tests/test_search_diagnostics.py`

1. Define native corridor metric/audit structs and an enum of stable decisions (`not_considered`, `same_as_incumbent`, `rejected_search_order`, `applied_exact_tie`; add a distinct unavailable-data decision only if required by an actual error path). Initialize the audit deterministically in `CoreSearchStatsInit`.
2. Replace `core_constrained_root_corridor_move` with a threshold-free proposal helper that examines only board-safe, root-allowed candidates with completed exact root values, computes `(immediate_exits desc, forced_steps asc, reachable desc)`, and returns the best proposal and its metrics. Keep the metrics board-derived; remove the length-12, forced-steps-3, and regression-delta constants.
3. After the completed root snapshot, record the common-selector incumbent and proposal. Copy for each side:
   - move and corridor metrics,
   - structural proof, relaxed capacity, and post-move length,
   - minimax score, outcome, bound, and terminal metadata.
4. Compute the sole override predicate from reusable semantic operations:
   - both records exist and are exact;
   - `core_search_value_semantically_equal` is true;
   - `CoreCompareRootCandidates(proposal, incumbent).ordering == CORE_ROOT_COMPARISON_EQUAL`;
   - proposal corridor metrics are strictly better.
   `INCOMPARABLE`, overlapping bounds, equal numeric score with different structure, or any strict search/structural preference must reject the proposal.
5. Apply a different proposal only when that full predicate is true. If applied, keep the existing score/value/move coherence and `corridor_guard` selection reason. Otherwise preserve the common-selector move, score, selection reason, and root comparison reason.
6. Serialize the audit to a stable Python dictionary in `py_core.c`; add matching `TypedDict` declarations. Use `None` for unavailable candidate values, not sentinel scores or magic move strings.
7. Rebuild with `python3 setup.py build_ext --inplace --force`. Expected: compiler exits 0 without new warnings.
8. Run the focused command from Task 1. Expected: PASS, including T424 `up` and T187 rejected override diagnostics.
9. Run `python3 -m pytest -q tests/test_search_diagnostics.py -k 'issue_33 or corridor or minimax_diagnostics' tests/test_issue_36_endgame.py`. Expected: all selected tests pass; #33/#36 decisions remain owned by `minimax`/the common selector.
10. Commit: `fix: constrain corridor guard to semantic ties`.

Acceptance coverage: no strictly better exact result is overwritten; no structurally dominated capacity-deficient proposal is selected; complete explainable guard activation/rejection diagnostics; no replay-specific production heuristics.

## Task 3: Make policy tooling consume the explicit guard contract

**Files:**

- Modify: `tools/check_duel_structural_policy.py:20-45, 180-220, 650-750`
- Modify: `tests/test_duel_structural_policy_checker.py:680-715, 900-935`
- Modify: `docs/duel-structural-policy.md` (or the existing nearest diagnostics contract document discovered with `rg`)

1. Replace the checker’s inference that any `selection_reason == corridor_guard` is an acceptable post-search exception. Read the explicit audit record and count an override only when `applied` and `exact_tie_permitted` are both true.
2. Treat an applied override with a false predicate, a structurally dominated selected proposal, or incoherent audit/search fields as a comparator violation/error. Preserve compatibility for diagnostics that lack the new field only where tests intentionally model historical records; do not silently bless them.
3. Add checker tests for:
   - rejected T424-style structural proposal is not an override and is not a selected structural violation;
   - equal numeric scores with incomparable structure do not permit an override;
   - a fully semantic-equal synthetic pair with better corridor metrics is counted as an allowed override;
   - a claimed applied override with a false predicate is rejected.
4. Document the order: common minimax/root comparison first, then exact semantic tie-only corridor proposal; explicitly state `INCOMPARABLE != EQUAL` and list audit fields/decisions.
5. Run `python3 -m pytest -q tests/test_duel_structural_policy_checker.py tests/test_issue_44_corridor_guard.py`. Expected: PASS.
6. Run `python3 tools/check_duel_structural_policy.py --help` and the repository’s fixture-mode checker command discovered from its tests. Expected: clean exit and zero unjustified post-search overrides for the five issue-44 positions.
7. Commit: `tools: audit corridor guard decisions explicitly`.

Acceptance coverage: diagnostics explain activations and rejected overrides; audit tooling no longer grants a blanket exception to corridor selections.

### Implemented ordering and audit contract

The permanent operational contract is maintained in
[`docs/duel-structural-policy.md`](../duel-structural-policy.md).

The common minimax/root comparison is authoritative. A different corridor
proposal may replace its incumbent only after both roots have exact,
semantically equal search values, `CoreCompareRootCandidates` returns `EQUAL`,
and the proposal has strictly better board-derived corridor metrics. An
`INCOMPARABLE` result is not an `EQUAL` result and never permits the override.

The top-level `corridor_guard` diagnostics record exposes `considered`,
`incumbent`, `proposal`, `comparison_ordering`, `comparison_reason`,
`exact_tie_permitted`, `applied`, and `decision`. Each candidate record carries
its move, corridor metrics, structural proof, relaxed capacity, post-move
length, and minimax score/outcome/bound. Stable decisions are
`not_considered`, `same_as_incumbent`, `rejected_search_order`, and
`applied_exact_tie`.

`tools/check_duel_structural_policy.py` counts a post-search override only when
the audit coherently reports an applied exact tie: the decision and comparison
must agree, the selected move must be the proposal, the audited candidates
must match their root records, both exact search records must be semantically
equal, and the proposal metrics must be better. Rejected, same-candidate, and
not-considered audits receive no exception and remain subject to ordinary root
policy checks. A missing audit for `selection_reason=corridor_guard`, an
incoherent applied claim, or a structurally dominated proposal is a comparator
violation.

The current fixed-depth re-audit of the five issue positions observed
`same_as_incumbent` at turns 290, 317, 187, and 284. Turn 424 observed a
`rejected_search_order` proposal because the common root comparison retained
the structurally safe incumbent. These statements describe the captured
diagnostics only; they do not establish that the historical replay outcome was
caused by the corridor decision.

## Task 4: Full regression, performance, and production-path verification

**Files:**

- Modify only if a test exposes a real contract defect; otherwise no code changes.

1. Rebuild production native extension: `python3 setup.py build_ext --inplace --force`. Expected: exit 0, no new warnings.
2. Focused: `python3 -m pytest -q tests/test_issue_44_corridor_guard.py`. Expected: all pass.
3. Predecessor/sibling suites: run issue #27/#33/#36/#38/#39/#41/#42/#43 tests discovered via `rg --files tests | rg 'issue_(27|33|36|38|39|41|42|43)'`, plus `tests/test_search_diagnostics.py` and `tests/test_duel_structural_policy_checker.py`. Expected: all pass or a previously documented optional-dependency skip only.
4. Native/C integration: run the repository’s C/native scripts and tests discovered in `tests/c` and `tests/scripts`; include minimax stats/bindings tests. Expected: all pass.
5. Broader non-training suite: `python3 -m pytest -q --ignore=tests/training`. Expected: all pass except documented existing skips.
6. Production-path replay audit: run all five issue-44 positions at fixed depths 8 and 11, deterministic node budgets 32k and 64k, and wall-clock budgets 100/300 ms. Assert no applied guard violates its predicate, T424 never selects `down` after a complete production-like root pass, and repeated node-budget runs are deterministic.
7. Measure median elapsed time/nodes over at least five deterministic runs per audit position before and after. The audit adds only root-level constant work; investigate any material latency regression rather than raising budgets.
8. Re-run #33 and #36 positive fixtures and record selected moves/reasons. Expected: decisions unchanged and no dependency on an impermissible guard override.
9. Inspect `git diff --check`, `git status --short`, `git log --oneline <base>..HEAD`, and build artifacts. Remove only artifacts created in this clean worktree; do not touch other worktrees.
10. If verification requires a correction, use failing-test-first TDD and commit `fix: address issue 44 verification finding`; otherwise make no empty commit.

Acceptance coverage: production path, adjacent regressions, deterministic budget behavior, latency, diagnostics coherence, and complete issue checkbox audit.

## Final acceptance mapping

- No strictly better exact minimax result is overwritten: Tasks 1-2 fixed-depth T424 assertion and semantic-equality predicate.
- No structurally dominated, capacity-deficient guard choice: Tasks 1-2 T424 structural assertions and `CoreCompareRootCandidates == EQUAL` gate.
- T424 chooses `up` in production-like search: Tasks 1, 2, and 4 across fixed, node, and wall-clock budgets.
- Every activation/rejection is explainable: Tasks 1-3 native audit dictionary, stable decisions, checker validation, and docs.
- #33/#36 remain green: Tasks 2 and 4 focused positive regressions.
- All five reported activations re-audited: Tasks 1 and 4 replay-derived fixture matrix.
- Non-goals preserved: no replay/game-ID production branches, no increased search budget as a fix, no broad evaluator retuning, and no regression-derived numeric thresholds.
