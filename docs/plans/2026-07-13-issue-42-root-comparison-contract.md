# Structural Root Comparison Contract Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Select Standard-duel root moves with an explicit lexicographic contract in which proved search outcomes remain authoritative, structural survivability precedes unresolved heuristic noise and PV stickiness, and diagnostics report the decisive comparison layer.

**Architecture:** Keep structural proof metadata separate from `CoreSearchValue`: introduce a typed root-comparison result that can decide terminal outcomes/bounds, structural dominance, forced-loss horizon, and unresolved heuristic value without encoding structure as a floating-point penalty. Run minimax for structurally analyzed candidates instead of prefiltering them before their search outcome is known, then use existing geometry and PV rules only when the stronger layers are equal. Preserve `strict_minimax` as the explicit numeric-search control policy and keep the post-search corridor guard unchanged for issue #44.

**Tech Stack:** C2x native search core, CPython extension diagnostics, pytest replay regressions, standalone C integration tests.

---

## Baseline and design decision

Fresh base: `origin/main` at `33b34cb5773ab7ec71d357192da6cc122632b116` (PR #47 merged).

Live issue #42 has no comments or linked active PR. On this base:

- `core_prepare_root_policy()` computes `SAFE`/`UNSAFE`/`UNKNOWN`, then removes structurally dominated roots before minimax (`core_algorithms.c:3935-4002`). That makes the #41 replay fixtures safe at the production default, but it cannot honor the explicit exception “unless the structurally proved candidate is itself a proved loss,” because the rejected candidate never receives a search value.
- `core_minimax_search()` compares `worst_reply.score` first (`core_algorithms.c:4747-4766`). Only exact numeric ties reach `core_equal_score_move_is_better()`, where previous PV is tested before generic reachable space (`core_algorithms.c:4326-4355`).
- Diagnostics expose only broad `selection_reason=minimax`; they do not say whether outcome, a valid bound, structural proof, forced-loss horizon, heuristic value, geometry, or PV decided the root.
- T169 on `strict_minimax`, depth 5 still chooses capacity-deficient `down`. With all positional weights zero except `center=1`, depth 1 produces `down=6 exact` and structurally safe `left=6 upper`; the current result has no inspectable ordering rationale.

Considered designs:

1. Add a huge capacity penalty to the evaluation score. Rejected: it conflates a proof lattice with tunable heuristics, can be defeated by user weights, and makes bound/outcome diagnostics misleading.
2. Keep the pre-search filter and run a second bounded “shadow minimax” only for rejected candidates. Rejected: it duplicates the production search, spends budget twice, and still cannot give one coherent bound/timeout contract.
3. Search eligible commands once and compare their tagged results lexicographically. Selected: it is the smallest general design that can put proved outcomes before structure, structure before unresolved heuristic/PV noise, and preserve conservative behavior for non-exact bounds.

The comparison must not infer an exact heuristic value from an upper/lower bound. A bound may decide the first layer only when it proves a terminal outcome ordering; unresolved overlapping bounds proceed to structure, then use numeric heuristic ordering only when the comparison is sound. No replay IDs, score-gap thresholds, or additive structural constants are allowed.

### Task 1: Define and unit-test the typed comparison semantics

**Files:**
- Modify: `battlesnake/c-core/core/search_stats.h:63-135`
- Modify: `battlesnake/c-core/core/search_stats.c:34-70`
- Modify: `battlesnake/c-core/core/core_algorithms.h:38-65`
- Modify: `battlesnake/c-core/core/core_algorithms.c:1410-1470, 4268-4356`
- Modify: `tests/c/test_battlesnake_strategy.c:1-440`

**Step 1: Write failing C contract tests**

Add table-driven tests which construct `CoreSearchValue` and `CoreRootCandidateStats` directly and call the public internal-core comparator. Cover:

- exact win/draw/unresolved/loss ordering before structural metadata;
- a proved non-loss versus a structurally safe proved loss;
- valid terminal upper/lower-bound dominance without treating an overlapping unresolved bound as exact;
- `SAFE` over `UNSAFE` and over `UNKNOWN` with `relaxed_static_capacity < post_move_length` when both search outcomes are unresolved;
- no structural dominance between `SAFE` and capacity-sufficient `UNKNOWN` unless another layer decides;
- longer terminal distance for two exact forced losses;
- exact unresolved heuristic value only after structural equality;
- equality returned for equal semantic candidates so deterministic/PV fallback remains a separate layer.

Use an ordering result (`candidate`, `incumbent`, `equal`) plus an enum reason, not a Boolean whose provenance is lost.

**Step 2: Run the C test and verify RED**

Run: `tools/run_c_server_tests.sh`

Expected: compilation fails because the comparison type/function and reason enum do not exist.

**Step 3: Implement the minimal pure comparator**

Define `CoreRootComparisonReason` values for at least `not_compared`, `terminal_outcome`, `search_bound`, `structural_proof`, `terminal_survival`, `heuristic_value`, `structural_tiebreak`, `previous_pv`, and `stable_direction`. Define a small comparison result containing ordering and reason. Implement the outcome/bound/structure/loss-horizon/heuristic layers as a side-effect-free function; do not read global state and do not add any score penalty.

Outcome proof must be conservative: exact outcomes are comparable; a one-sided bound decides only when its direction proves the candidate cannot cross the incumbent’s outcome class. Overlapping or merely unresolved bounds must not claim outcome dominance. Both exact forced losses compare `terminal_distance` explicitly before their encoded scores.

**Step 4: Run tests and verify GREEN**

Run: `tools/run_c_server_tests.sh`

Expected: all four standalone C binaries pass, including the new comparator cases.

**Step 5: Commit**

```bash
git add battlesnake/c-core/core/search_stats.h battlesnake/c-core/core/search_stats.c battlesnake/c-core/core/core_algorithms.h battlesnake/c-core/core/core_algorithms.c tests/c/test_battlesnake_strategy.c
git commit -m "feat(search): define root comparison semantics"
```

### Task 2: Integrate structural ordering after search outcomes are available

**Files:**
- Create: `tests/test_issue_42_root_comparison.py`
- Modify: `battlesnake/c-core/core/core_algorithms.c:3728-4017, 4270-4775, 4828-5018`
- Modify: `tests/test_issue_41_branching_pockets.py:420-540, 744-760`
- Test: `tests/fixtures/issue_41_branching_pocket_positions.json`

**Step 1: Write failing production-path regressions**

In `tests/test_issue_42_root_comparison.py`, reuse the committed T169 fixture and build weights with every positional term zero except `center=1.0`. Assert under `standard_ladder_opportunity`, fixed depth 1:

- both `down` and structurally safe `left` receive tagged minimax values (the current prefilter makes this fail);
- `left` is selected although the unresolved numeric/bound ordering alone can favor or retain `down`;
- the selected move’s outcome is not a proved loss;
- the comparison reason is structural, once Task 3 exposes it.

Add a board-level outcome-precedence regression using the existing immediate-win geometry from `test_structural_dominance_preserves_guaranteed_immediate_win`: structurally `UNKNOWN` `down` is an exact win and must still beat structurally `SAFE` `up`. Add a direct comparator/integration regression in which a structurally safe value is an exact loss and a weaker structural candidate is a proved non-loss. This may use the typed comparator from Task 1 if a minimal legal board cannot express the state without unrelated geometry.

Update #41 expectations from “not searched because prefiltered” to the stronger invariant “searched, structurally dominated at comparison time, and not selected unless it has a proved superior outcome.” Keep proof/capacity assertions unchanged.

**Step 2: Run focused tests and verify RED**

Run:

```bash
pytest -q tests/test_issue_42_root_comparison.py tests/test_issue_41_branching_pockets.py
```

Expected: T169 fails because structural candidates are filtered before minimax and the root loop still compares numeric score first.

**Step 3: Move structural dominance into the root comparator**

Retain immediate command legality and proved immediate worst-reply gates in `root_allowed_mask`, but stop using `SAFE`/`UNSAFE`/capacity metadata as a pre-search eligibility decision. In particular, remove the structural-dominance mask deletion that prevents a later proved outcome from participating. Do not broaden this task into corridor-guard behavior (#44) or budget scheduling (#43).

At `ply == 0` under `CORE_ROOT_POLICY_STANDARD_LADDER_OPPORTUNITY`, compare each complete root candidate through the typed comparator before generic tie-breaking. Under `CORE_ROOT_POLICY_STRICT_MINIMAX`, preserve the existing numeric-search control semantics. Below root, preserve ordinary alpha-beta numeric ordering.

When an unresolved candidate is cut off with an upper bound, preserve the bound tag and do not silently promote it to exact. Structure may decide between unresolved candidates whose bound relationship does not prove a terminal outcome. Proved win/draw/non-loss must still beat a structurally stronger proved loss.

**Step 4: Verify GREEN and search coherence**

Run:

```bash
pytest -q tests/test_issue_42_root_comparison.py tests/test_issue_41_branching_pockets.py tests/test_issue_38_dead_tunnel.py tests/test_issue_36_endgame.py
pytest -q tests/test_search_diagnostics.py -k 'issue_30 or issue_33 or corridor or forced_loss'
```

Expected: all pass; selected `score`, `minimax_outcome`, and `minimax_bound` still match the selected candidate.

**Step 5: Commit**

```bash
git add battlesnake/c-core/core/core_algorithms.c tests/test_issue_42_root_comparison.py tests/test_issue_41_branching_pockets.py
git commit -m "fix(search): apply structural root comparison after outcomes"
```

### Task 3: Make deterministic fallback order and diagnostics explicit

**Files:**
- Modify: `battlesnake/c-core/core/core_algorithms.c:1220-1235, 4270-4356, 4740-5018`
- Modify: `battlesnake/c-core/core/search_stats.h:105-145`
- Modify: `battlesnake/c-core/core/search_stats.c:34-70`
- Modify: `battlesnake/c-core/py-core/py_core.c:480-520, 760-792`
- Modify: `battlesnake/battlesnake_native.pyi:1-180`
- Modify: `tests/test_search_diagnostics.py:25-45, 286-410`
- Modify: `tests/test_issue_42_root_comparison.py`

**Step 1: Write failing diagnostics and determinism tests**

Add `root_comparison_reason` to the exact diagnostics-key contract. Add repeated-run tests for:

- T169 reporting `structural_proof` as the decisive layer;
- the immediate-win fixture reporting `terminal_outcome` rather than structure;
- an exact unresolved heuristic comparison reporting `heuristic_value`;
- equal structural/search candidates producing the same move with move ordering enabled and disabled;
- an exact heuristic tie in which previous iteration/PV points to the smaller reachable region: geometry must decide before PV;
- truly equal geometry falling through to `previous_pv` or `stable_direction`, with identical results across repeated runs.

The test must compare full tagged candidate values and the chosen move, not only a string field.

**Step 2: Run tests and verify RED**

Run:

```bash
pytest -q tests/test_issue_42_root_comparison.py tests/test_search_diagnostics.py
```

Expected: diagnostics key/reason assertions fail and the PV-before-reachable fixture exposes the old order.

**Step 3: Propagate the decisive reason**

Track the comparison reason alongside the current root best and snapshot it with the last completed iterative-deepening result. Preserve it when a later iteration times out. Expose it as `root_comparison_reason` in Python diagnostics and the `.pyi` TypedDict/interface. Keep broad `selection_reason` for timeout/corridor compatibility.

Refactor `core_equal_score_move_is_better()` so deterministic structural geometry (corridor/tail/reachable comparisons) precedes previous PV, and stable original direction is last. Assign the exact matching reason for each fallback. Do not use PV to override a stronger certificate or geometry.

**Step 4: Run tests and verify GREEN**

Run:

```bash
python3 setup.py build_ext --inplace --force
pytest -q tests/test_issue_42_root_comparison.py tests/test_search_diagnostics.py
tools/run_c_server_tests.sh
```

Expected: warning-free native build and all tests pass deterministically.

**Step 5: Commit**

```bash
git add battlesnake/c-core/core/core_algorithms.c battlesnake/c-core/core/search_stats.h battlesnake/c-core/core/search_stats.c battlesnake/c-core/py-core/py_core.c battlesnake/battlesnake_native.pyi tests/test_search_diagnostics.py tests/test_issue_42_root_comparison.py
git commit -m "feat(search): report root comparison reasons"
```

### Task 4: Lock the acceptance contract across regressions and production paths

**Files:**
- Modify: `tests/test_issue_42_root_comparison.py`
- Modify: `tests/c/test_battlesnake_strategy.c`
- Modify: `docs/plans/2026-07-13-issue-42-root-comparison-contract.md` (append measured verification evidence only)

**Step 1: Add the missing cross-path assertions before implementation changes**

Mirror the representative structural/outcome precedence cases through `CoreMinimaxMoveWithStats()` in the standalone C strategy test. Add a repeated production-budget matrix for T169 (100/200/300 ms, at least three repeats) asserting the bad move is not selected and comparing reasons only if scheduler-driven runs happen to complete the same depth. Prove reason stability non-vacuously with a separate repeated fixed-depth frontier. Ensure the tests do not assert a particular safe direction when `up` and `left` are semantically equal.

**Step 2: Verify the new assertions fail if the comparator is bypassed**

Temporarily run the focused tests against `origin/main` (or use `git show`/a clean detached worktree) and record that at least the diagnostics/eligibility assertion fails for the expected reason. Do not commit any temporary instrumentation.

**Step 3: Run the complete issue validation**

Run:

```bash
python3 setup.py build_ext --inplace --force
pytest -q tests/test_issue_42_root_comparison.py tests/test_issue_41_branching_pockets.py tests/test_issue_38_dead_tunnel.py tests/test_issue_36_endgame.py tests/test_search_diagnostics.py tests/test_duel_structural_policy_checker.py
tools/run_c_server_tests.sh
tools/run_c_position_eval_tests.sh
pytest -q -k 'not training'
```

Also run the repository’s #41 corpus checker over the available 49-game export and compare search latency/nodes for T169/T439/T288 at 100/200/300 ms against `33b34cb`. Acceptance is zero selected structural-policy violations, identical terminal/corridor outcomes, no material production-latency regression, and no warning from the forced native build. Report host variance rather than inventing a threshold after seeing results.

**Step 4: Append exact evidence and commit**

Append commands, pass counts, replay moves/depths/reasons, corpus counts, and benchmark deltas to this plan. Do not claim causality beyond the measured fixtures.

```bash
git add tests/test_issue_42_root_comparison.py tests/c/test_battlesnake_strategy.c docs/plans/2026-07-13-issue-42-root-comparison-contract.md
git commit -m "test(search): cover root comparison contract"
```

## Acceptance-criteria mapping

- Structural proof/capacity before PV: Tasks 1-3 typed structural layer and PV-last regressions.
- Safe non-losing candidate dominates capacity-deficient unknown: Tasks 2 and 4 T169 plus production-budget matrix.
- Terminal outcomes and valid bounds retain precedence: Tasks 1-2 synthetic contract table and immediate-win integration fixture.
- Equal candidates deterministic: Task 3 ordering-on/off and repeated-run fixtures.
- Exact comparison reason in diagnostics: Task 3 `root_comparison_reason` enum/binding/tests.
- Forced-loss survival and corridor regressions: Tasks 2 and 4 #30/#33/#36/#38/#39/search suites.
- Non-goals: no game-ID branch, no score-gap threshold, no additive proof penalty, no corridor-guard redesign, and no budget increase.

## Verification evidence

Verified on 2026-07-14 against base `33b34cb5773ab7ec71d357192da6cc122632b116`; the final Task 4 production fix is `58338ffeaefc0bfe28c36d1f5973479e5af8518b`.

### Baseline RED and root-cause evidence

After `python3 setup.py build_ext --inplace --force` in the detached base worktree, a read-only diagnostic over the committed fixtures reported:

- T169: `root_comparison_reason present: False`; deficient `down` was `allowed=False`, `score=None`, proof `unknown`, capacity/length `1/12`, while `up` and `left` were SAFE and searched. This proves the base filtered the deficient root before outcome comparison.
- The 6x6 equal-score PV/reachable fixture had reachable space `left=30`, `right=1`, equal scores `1.0/1.0`, but selected `right`. This proves the old PV rule preceded deterministic reachable geometry.

Task 4's first 49-file audit exposed one additional genuine comparator fallback: game `8e68e0d4-b84f-4b5a-bda5-e63c1841a26e`, turn 659 selected deficient UNKNOWN `right` (exact loss, score -991000, distance 9, capacity/length 56/59) over SAFE `up` (upper loss, score -991000, distance 9, capacity/length 59/58). The outcome intervals were equal and numeric intervals merely touched, so no strict search layer proved `right` superior. A deterministic C selector regression failed before the fix because loss-tagged roots skipped structural pruning; touching numeric bounds retained both roots and geometry selected the deficient root. The minimal fix preserves unresolved structural-before-heuristic ordering, while applying structure to other touching/overlapping frontiers only after strict outcome, all-exact-loss distance, and numeric-interval pruning. It does not change the corridor guard.

### Focused, native, and broad validation

Commands and results:

- `python3 setup.py build_ext --inplace --force`: PASS, no compiler warnings.
- `pytest -q tests/test_issue_42_root_comparison.py tests/test_issue_41_branching_pockets.py tests/test_issue_38_dead_tunnel.py tests/test_issue_36_endgame.py tests/test_search_diagnostics.py tests/test_duel_structural_policy_checker.py`: exit 0; 166 tests collected.
- `tools/run_c_server_tests.sh`: PASS, including production `CoreMinimaxMoveWithStats()` structural and terminal precedence cases plus the touching-bound regression.
- `tools/run_c_position_eval_tests.sh`: PASS in all four build configurations.
- Standalone strategy compilation with `gcc -Wall -Wextra -Werror` and `CORE_ROOT_SELECTION_TESTING`: PASS.
- The requested `pytest -q -k 'not training'` cannot collect on this host because three `tests/training` modules import unavailable `pandas` before `-k` selection. The equivalent environment-scoped `pytest -q tests --ignore=tests/training` passed (279 collected, one displayed skip, exit 0). No test was weakened or dependency failure classified as a code failure.

### Production-budget replay matrix

Final HEAD, three consecutive repetitions for each budget:

- T169: 100 ms selected `up`, depth 4, reason `heuristic_value`; 200/300 ms selected `up`, depth 5, reason `previous_pv`. The recorded bad `down` remained searched, UNKNOWN and capacity-deficient (`1/12`); every selected root was SAFE. Across separately controlled runs the semantically tied safe result also reached `left` at depths 6-7, so no wall-clock depth or one safe direction is asserted. Equal-depth wall-clock observations are compared when they occur, but the test does not require the scheduler to produce a repeated depth. A separate fixed-depth test repeats the identical tagged frontier and reason three times.
- T439: 100 ms selected `up`, depth 8; 200/300 ms selected `up`, depth 9; all reasons `structural_proof`. The bad candidate remained UNKNOWN `1/36`; selected proof SAFE.
- T288: 100 ms selected `down`, depth 6; 200/300 ms selected `down`, depth 7; all reasons `structural_proof`. The bad candidate remained UNKNOWN `3/32`; selected proof SAFE.

Every run used `selection_reason=timeout_best_so_far`, retained coherent outcome/bound/score tags for searched roots, and never selected its recorded bad move. The repeated test intentionally asserts semantic coherence rather than timing-sensitive depth or tied-safe-direction identity.

### Controlled base/HEAD latency comparison

Five back-to-back 100 ms runs per fixture on the same host/configuration:

- T169 base: 97.970-98.142 ms, 2501-2675 nodes, depth 6, `left`; HEAD: 98.248-98.357 ms, 3387-3504 nodes, depth 6, `left`.
- T439 base: 98.782-99.194 ms, 29211-34567 nodes, depth 9, `up`; HEAD: 99.207-99.358 ms, 30265-32704 nodes, depth 9, `up`.
- T288 base: 96.081-96.219 ms, 16083-16284 nodes, depth 8, `down`; HEAD: 96.375-96.523 ms, 15623-15767 nodes, depth 7, `down`.

Measured end-to-end deltas were below 0.45 ms. T169 examines the previously filtered deficient root and therefore visits more nodes; T439/T288 node ranges overlap or decrease. The T288 depth difference is reported as measured host/search behavior, not converted into a post-hoc threshold. Moves and structural safety criteria remained correct.

### Full corpus audit

Command:

```bash
python3 tools/check_duel_structural_policy.py \
  --export-root /home/sergei-scv/temp/playing-battlesnake/exports/zkh-dot_lost_games \
  --budget-ms 100 --json
```

The checker now independently recomputes strict outcome intervals, numeric intervals after prior layers, and all-exact-loss terminal distance; it never exempts a record from `root_comparison_reason` text. Touching intervals remain non-strict. Post-search corridor overrides are reported separately for issue #44.

After quality review tightened the checker to reproduce production's structural-before-unresolved-heuristic and exact-loss-survival-before-numeric ordering, the full corpus was rerun. Final result: exit 0; files 49; replay roots seen 16545; Standard-duel roots 16496; prefiltered 91; diagnostics 16405; unknown proofs 11167; cutoffs `deadline=5763`, `horizon=1437`, `policy_sufficient=546`, `resource_limit=52`, `survivability=3369`; strictly justified search selections 15; comparator violations 0; errors 0. Two timing-dependent post-search overrides remained visible (`091dc137...` T376 and `1985bf57...` T424). Their count is not asserted because proof/depth completion varies under a 100 ms wall-clock budget; neither is hidden or claimed fixed by issue #42.

Quality-review RED tests proved that the earlier checker would incorrectly accept both a deficient UNKNOWN exact-unresolved score 100 over a structurally dominating SAFE exact-unresolved score 0, and a shorter exact forced loss with an artificially larger score. Both were misclassified as `numeric_interval`. The corrected checker rejects the first before heuristic numeric comparison, and for an all-exact-loss frontier requires strictly longer terminal distance (equal distances alone may continue to numeric ordering). `tests/test_duel_structural_policy_checker.py` passed all 39 tests. The issue42/diagnostics focused command passed three controlled repetitions, each with 48 tests and 11 subtests; later review moved the non-vacuous stability guarantee to a deterministic fixed-depth test rather than scheduler-dependent repeated depths.

### Live-review non-finite bound consistency

The live PR review identified that global numeric interval pruning accepted non-finite scores for non-exact bounds even though `CoreCompareRootCandidates()` returned INCOMPARABLE and the Python checker rejected them. A selector-level matrix over LOWER/UPPER with NaN, positive infinity, and negative infinity against a finite exact root and against every LOWER/UPPER exceptional pairing reproduced RED: the global selector incorrectly reported `SEARCH_BOUND` dominance. The fix makes numeric interval construction fail conservatively for every non-finite score. Existing exact/exact unresolved finite preference still runs before interval validity, preserving its explicit contract; non-exact or all-nonfinite frontiers remain nonempty and use a deterministic geometry/PV/stable-direction fallback without claiming numeric dominance. The final Cartesian test covers both finite/exceptional role assignments, both original move orders, all four bound-kind pairs, every 3x3 exceptional-score pairing, and repeated selection per order. Comparator results are symmetric: identical tagged positive- or negative-infinity records with the same bound kind are EQUAL (semantic equality, not dominance), while NaN, different exceptional values, different bound kinds, and finite-exact versus exceptional-bound records are INCOMPARABLE. Python `_numeric_interval()` rejection covers all six non-finite bound variants.

Post-fix validation: forced native rebuild warning-free; issue42/diagnostics/checker focused tests `93 passed, 11 subtests`; C server suite passed; standalone strategy compile/run with `-Wall -Wextra -Werror` passed; #41/#38/#36 regressions `80 passed, 1 skipped, 14 subtests`; all four C position-evaluation configurations passed; broad non-training suite exit 0 with 287 tests collected. No corridor code or behavior changed.

A final quality review found that the public exact/exact comparator returned `INCOMPARABLE/heuristic_value` immediately for all-nonfinite pairs, making semantic equality unreachable even though the global selector already retained those roots for fallback. Direct bidirectional comparator RED cases now cover exact positive-infinity identity, negative-infinity identity, NaN identity, positive versus negative infinity, and both infinities versus NaN. The comparator now returns early only for decisive finite preference (`CANDIDATE` or `INCUMBENT`); otherwise it reaches semantic equality. Identical exact positive/negative infinity records therefore return `EQUAL/not_compared`, while NaN and differing nonfinite values return `INCOMPARABLE/not_compared`. Finite-over-nonfinite exact preference remains `HEURISTIC_VALUE`, and global selector behavior is unchanged. The focused issue42/diagnostics/checker suite again passed `93 tests, 11 subtests`; C server and strict warning compilation passed.

`git diff --check` passed. No replay exports, native libraries, build products, or other generated artifacts are tracked by this task.

### Live-review mixed exact-loss frontier

The final live rereview exposed a narrower ordering bug: exact-loss terminal survival was enforced only when every active root was an exact loss. In a mixed frontier containing exact losses at distances 10 and 5 plus an upper-bound loss, the bounded third root disabled the pairwise exact-loss rule and allowed the shorter exact loss to win on score. A six-permutation C selector regression reproduced that failure, while a checker regression proved that the audit made the same mistake by accepting the shorter exact loss through `numeric_interval`.

The fix applies terminal-survival dominance to the exact-loss subset whenever at least two exact losses are active. It removes only exact losses shorter than the longest exact loss and retains all bounded/non-exact roots for later comparison. Thus the bounded root can still legitimately win; across the six input permutations the regression observes both the longer exact loss and the bounded root, and never the shorter exact loss. The checker now performs the same pairwise rule regardless of unrelated roots; equal exact-loss distances remain eligible for later numeric comparison.

Post-fix validation: forced native rebuild passed without warnings; issue42/diagnostics/checker focused tests passed `94 tests, 11 subtests`; C server tests and standalone strategy compilation/run with `-Wall -Wextra -Werror` passed; #41/#38/#36 regressions passed `80 tests, 1 skipped, 14 subtests`; all four C position-evaluation configurations passed. The broad non-training suite reached `285 passed, 1 skipped, 52 subtests`; its only two failures were localhost socket creation denied by the filesystem sandbox, and those exact native-server integration tests passed `2 tests, 3 subtests` when rerun with permission to bind localhost. `git diff --check` passed.

### Checker active-frontier parity

Quality review found that the checker compared terminal survival only against the current SAFE alternative. RED diagnostics selected a deficient exact loss at distance 10 over a SAFE exact loss at distance 5 while a third allowed, searched deficient/UNKNOWN exact loss at distance 20 remained active. The checker incorrectly reported `terminal_survival`, although production removes every exact loss shorter than the active exact-loss maximum before later layers.

The checker now independently derives that maximum from every allowed candidate with a populated search score and an exact-loss distance. A selected exact loss below that maximum cannot claim terminal survival or fall through to numeric justification. Control cases prove that a distance-20 root which is disallowed or unsearched is outside the active search frontier; the selected distance-10 root may then strictly dominate the SAFE distance-5 alternative. No diagnostic reason string is trusted, and no production code changed.

Validation: the new negative test failed before the checker change with `justified_by_search=True` and `terminal_survival`; both inactive-frontier controls were already green. After the fix, `pytest -q tests/test_duel_structural_policy_checker.py` passed `49 tests`, including the corpus-shaped replay/checker cases, and `pytest -q tests/test_issue_42_root_comparison.py tests/test_search_diagnostics.py` passed `48 tests, 11 subtests`. `git diff --check` passed.

### Checker active-record precondition

The next quality rereview found that active-frontier membership constrained the global exact-loss maximum but not the two records passed to strict pairwise comparison. Four RED cases showed that an unsearched or disallowed selected exact loss, and an unsearched or disallowed SAFE alternative, could still yield `terminal_survival` and hide a structural violation. The selected-root cases include another active exact loss at distance 10, so the failure cannot be accidentally prevented by the global-maximum check.

A single `_is_active_search_record()` predicate now defines membership as `allowed=True` with a populated minimax score. Both pairwise records must satisfy it before outcome, terminal-survival, or numeric justification, and the exact-loss maximum uses the same predicate to prevent semantic drift. The earlier positive controls remain: an inactive third longer exact loss does not block two active roots from establishing terminal survival. Reason strings remain ignored and production code is unchanged.

Validation: all four new cases failed before the predicate gate with `justified_by_search=True` and passed afterward; the two positive inactive-third controls also pass. The full checker suite passed `53 tests`, and issue42/diagnostics passed `48 tests, 11 subtests`. Python compilation and `git diff --check` passed.

### Atomic timeout-snapshot source and deterministic reason stability

Live exact-head review found that a first-iteration partial root snapshot atomically adopted its move, value, comparison reason, and tagged roots at depth zero, while `selection_reason` remained the initializer's `allowed_fallback` because it was derived later only when `completed_depth > 0`. The deterministic C seam reproduced RED by requiring the snapshot depth and source alongside the records: the old seam left those outputs stale and failed on the completed-depth assertion.

`CoreRootIterationSnapshot` now owns the selection source. A completed iteration snapshots `minimax`; an adopted first partial snapshots `timeout_best_so_far`; an untouched fallback remains `allowed_fallback`. Stats publish that atomic field directly. The C seam repeats the completed snapshot three times and verifies identical depth, source, comparison reason, selected value, and root tags; it separately verifies adopted-partial and no-search transitions overwrite prior outputs without stale source or tags. No new public reason value was necessary, and comparator, corridor, and budget behavior are unchanged.

The 100/200/300 ms replay smoke retains unconditional move, structural proof, search-tag, and selected-value coherence assertions, but no longer requires three scheduler runs to contain a repeated completed depth. If equal-depth observations occur their reasons must agree. A separate three-run fixed-depth test requires depth 2, one identical tagged frontier, and one non-`not_compared` reason, making stability coverage non-vacuous without a wall-clock assumption.

Validation: forced native rebuild passed without warnings; focused issue42/diagnostics/checker passed `102 tests, 11 subtests`; C server and strict `-Wall -Wextra -Werror` strategy tests passed; #36/#38/#41 passed `80 tests, 1 skipped, 14 subtests`; all four C position configurations passed. Broad non-training/non-server tests passed `293 tests, 1 skipped, 52 subtests`, and the localhost native-server integration suite separately passed `2 tests, 3 subtests`. `git diff --check` passed.
