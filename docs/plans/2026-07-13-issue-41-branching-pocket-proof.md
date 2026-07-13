# Branching Pocket Structural Proof Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Replace the `open_branch` shortcut with a bounded, tri-state full-body structural proof so capacity-deficient duel pockets cannot outrank a proved non-losing alternative.

**Architecture:** Add a root-only structural proof that explores every surviving own-body branch with complete body coordinates, backs results up as `safe`, `unsafe`, or `unknown`, and reports why the proof terminated. Reuse the existing opponent-arrival model to conservatively disallow exits an equal-or-longer opponent can close; then apply an explicit proof-class dominance relation before minimax score/PV ordering, never as a floating-point penalty.

**Tech Stack:** C2x native search core, CPython extension bindings, Python 3.10, pytest, replay-derived JSON fixtures, existing native C test runners.

---

## Issue contract and chosen design

The baseline at `f02f34dd9d27e689af5608da97dfc0e004598ac6` reproduces the defect:

- `7351410a...` T169 at 300 ms selects `down` with `open_branch`, capacity 1, length 12 while `up`/`left` have capacity 109.
- `0188bbac...` T288 at 300 ms selects `left` with `open_branch`, capacity 3, length 32 while `down` has capacity 86.
- `74f38216...` T439 reports `right` as `open_branch`, capacity 1, length 36 and selects it at deeper search; the 300 ms choice is host-dependent, so the regression must assert the structural invariant rather than a baseline timing accident.

Two alternatives were evaluated:

1. Reject every candidate where static capacity is smaller than body length. This is cheap but incorrect: tail release, repeatable cycles, and genuine exits can make a static deficit survivable, and an opponent-controlled doorway can make an apparent capacity surplus unusable.
2. Explore the bounded graph of complete body states and use static/time-aware capacity only as a proof certificate. This is the selected design. It is more work, but it directly models branches and cycles and can return `unknown` on deadline/resource exhaustion instead of inventing safety.

Proof semantics:

- `safe`: at least one explored own continuation reaches a capacity-sufficient uncontested region, a repeatable uncontested full-body cycle, or the body-length-derived proof horizon without ever entering an unresolved deficit.
- `unsafe`: every surviving continuation is exhaustively refuted before that horizon.
- `unknown`: allocation failure, overall search deadline, or the explicit resource guard prevents complete backup. Resource guards affect completeness only; they must never turn into `safe` or `unsafe`.
- A state key includes ordered body coordinates (not only the head). Depth/time is included wherever opponent-arrival deadlines make the same body geometry semantically different.
- Opponent closure is conservative: use the existing equal-or-longer-opponent arrival computation and forbid proof edges whose destination can be occupied no later than our arrival. A cycle is a safety certificate only if its repeated edges remain uncontested, not merely because the own-only geometry repeats.
- Under `standard_ladder_opportunity`, if a candidate with an alive opponent reply is structurally `safe`, then any capacity-deficient candidate whose proof is `unsafe` or `unknown` is structurally dominated and excluded before score/PV ordering. `strict_minimax` remains an explicit control policy.

## Acceptance mapping

- AC1/AC2 (`open_branch`, complete body state): Tasks 1 and 2.
- AC3 (opponent-controlled exits): Task 1 synthetic doorway regression and opponent-arrival integration.
- AC4 (structural dominance before heuristic/PV): Task 2.
- AC5 (three production-budget replays): Task 2.
- AC6 (#33/#36/#38/#39 regressions): Task 3.
- AC7 (proof diagnostics): Tasks 1 and 2.
- Non-goals (no replay IDs in production, no capacity-only rejection, no budget-only fix): enforced in every review and Task 3 final audit.

### Task 1: Add the tri-state full-body structural proof engine

**Files:**
- Modify: `battlesnake/c-core/core/search_stats.h:20-79`
- Modify: `battlesnake/c-core/core/search_stats.c:45-66`
- Modify: `battlesnake/c-core/core/core_algorithms.c:1514-1735`
- Modify: `battlesnake/c-core/py-core/py_core.c:440-575`
- Modify: `battlesnake/battlesnake_native.pyi:65-90`
- Modify: `tests/c/test_battlesnake_strategy.c:220-270`
- Test: `tests/test_issue_41_branching_pockets.py`

**Requirements:**

Add explicit enums for `CoreStructuralProofResult` (`NOT_ANALYZED`, `SAFE`, `UNSAFE`, `UNKNOWN`) and `CoreStructuralProofCutoff` (`NONE`, `CAPACITY`, `CYCLE`, `HORIZON`, `DEAD_END`, `DEADLINE`, `RESOURCE_LIMIT`, `ALLOCATION_FAILURE`). Extend each `CoreRootCandidateStats` with proof result, cutoff, horizon, explored-state count, proved capacity, and `opponent_closure_considered`. Do not overload `CoreTrapStatus`; keep it as backward-compatible legacy diagnostics until callers migrate.

Replace the single-successor loop with a DFS/backtracking proof over `CoreSearchState`. Each node must enumerate all four commands, make/unmake each command, and key visited states by the complete ordered body. Back up `SAFE` if any child is safe, `UNSAFE` only when every child is unsafe, and otherwise `UNKNOWN`. Derive the horizon from post-move body length. Use a named resource guard only to bound memory/work and report `UNKNOWN` when it fires.

Precompute opponent arrival deadlines from the real root board using the existing vacate/arrival primitives near `core_fill_opponent_arrival()`. At proof depth `d`, reject an edge when an equal-or-longer opponent can occupy its destination at or before absolute turn `d + 1`; set `opponent_closure_considered=true` whenever opponent arrivals participate. Capacity/cycle certificates must remain valid under these closure deadlines.

**Step 1: Write failing native and Python tests**

Add synthetic boards that demonstrate:

```python
def test_branching_pocket_is_exhaustively_proved_unsafe():
    # Two live children, both end before the body-length horizon.
    assert candidate["structural_proof"] == "unsafe"
    assert candidate["explored_states"] > 1

def test_repeatable_branch_cycle_is_proved_safe():
    assert candidate["structural_proof"] == "safe"
    assert candidate["proof_cutoff"] == "cycle"

def test_opponent_controlled_doorway_is_not_a_safe_exit():
    assert candidate["opponent_closure_considered"] is True
    assert candidate["structural_proof"] != "safe"
```

Mirror at least the unsafe branching and safe-cycle contracts in `tests/c/test_battlesnake_strategy.c` so the native API is tested without the Python binding.

**Step 2: Run tests to verify RED**

Run:

```bash
python3 setup.py build_ext --inplace --force
python3 -m pytest tests/test_issue_41_branching_pockets.py -q
tools/run_c_server_tests.sh
```

Expected: FAIL because structural proof fields/results do not exist and `open_branch` stops at the first fork. Confirm failures are assertions about the new contract, not fixture/import errors.

**Step 3: Implement the minimal proof engine**

Implement the enums, initialized fields, full-body visited-state representation, tri-state recursion, conservative opponent-arrival check, exact backup rules, public diagnostic field serialization/type declarations, and cleanup on every error/cutoff path. Avoid test-only APIs and replay-specific branches.

**Step 4: Rebuild and verify GREEN**

Run the same three commands. Expected: all new synthetic tests and native runner pass; compiler emits no new warnings.

**Step 5: Commit**

```bash
git add battlesnake/c-core/core/search_stats.h battlesnake/c-core/core/search_stats.c battlesnake/c-core/core/core_algorithms.c battlesnake/c-core/py-core/py_core.c battlesnake/battlesnake_native.pyi tests/c/test_battlesnake_strategy.c tests/test_issue_41_branching_pockets.py
git commit -m "fix(search): prove branching pocket structure"
```

### Task 2: Integrate proof dominance, replay fixtures, and public diagnostics

**Files:**
- Create: `tools/build_issue_41_fixtures.py`
- Create: `tests/fixtures/issue_41_branching_pocket_positions.json`
- Modify: `tests/test_issue_41_branching_pockets.py`
- Modify: `battlesnake/c-core/core/core_algorithms.c:1865-1995`

**Requirements:**

The fixture builder must deterministically extract T169, T439, and T288 from replay exports, normalize only board state needed by tests, and contain no production dependency on replay IDs. Add two compact synthetic fixtures: a contestable doorway and a genuine safe branch/cycle.

Use the Task 1 public diagnostics `structural_proof`, `proof_cutoff`, `proof_horizon`, `structural_capacity`, `explored_states`, and `opponent_closure_considered` in the replay assertions.

Replace `core_structural_alternative()` with explicit proof semantics. Under the Standard opportunity policy, identify a `SAFE` candidate with at least one alive reply. Before minimax starts, remove from `root_allowed_mask` every other candidate for which `relaxed_static_capacity < post_move_length` and proof is not `SAFE`. Add a dedicated rejection reason such as `structurally_dominated`; do not encode ordering through a large score penalty. Preserve fail-open behavior when there is no proved-safe alternative, and preserve `strict_minimax` behavior.

**Step 1: Add replay-derived failing tests**

For each representative board, assert at budgets 100, 200, and 300 ms that the recorded bad move is not selected, the bad move is capacity-deficient and not `SAFE`, and at least one allowed alive-reply alternative is `SAFE`. For T439 also use deterministic fixed-depth coverage so the regression is independent of host speed.

Add a direct dominance test with deliberately close/equal heuristic values and reversed move order, proving the result comes from `root_allowed_mask`, not a floating-point delta or PV tie-break.

**Step 2: Verify RED**

Run:

```bash
python3 -m pytest tests/test_issue_41_branching_pockets.py -q
```

Expected: FAIL on the recorded bad moves/allowed masks because dominance is not integrated yet.

**Step 3: Implement minimal integration**

Add the explicit dominance relation. Keep the proof layer independent of heuristic evaluation and leave strict minimax available for control tests.

**Step 4: Verify GREEN and production path**

Run:

```bash
python3 setup.py build_ext --inplace --force
python3 -m pytest tests/test_issue_41_branching_pockets.py -q
tools/run_c_server_tests.sh
```

Expected: all representative/synthetic tests pass at the stated budgets and native-server tests pass.

**Step 5: Commit**

```bash
git add tools/build_issue_41_fixtures.py tests/fixtures/issue_41_branching_pocket_positions.json tests/test_issue_41_branching_pockets.py battlesnake/c-core/core/core_algorithms.c
git commit -m "fix(search): prioritize proved structural safety"
```

### Task 3: Add corpus invariant coverage and complete regression/performance validation

**Files:**
- Create: `tools/check_duel_structural_policy.py`
- Modify: `tests/test_issue_41_branching_pockets.py`
- Modify: `README.md` or `docs/native-duel-performance.md` only if the diagnostic invocation needs user-facing documentation

**Requirements:**

Create a read-only corpus checker that loads replay exports, constructs each Standard 1v1 root board, runs production diagnostics, and reports a violation when the selected move is capacity-deficient and not `SAFE` while an alive-reply `SAFE` alternative exists. The checker must report game ID/turn as evidence only; IDs must never influence policy. It must return non-zero if violations are found and support a deterministic budget argument.

Test the checker against the committed issue fixture set, including one deliberately supplied pre-fix-shaped diagnostics record to prove the detector actually flags the signature and one compliant record to prevent an always-fail implementation.

**Step 1: Write failing checker tests**

Add tests for violation detection, compliant selection, and stable structured output. Run:

```bash
python3 -m pytest tests/test_issue_41_branching_pockets.py -q
```

Expected: FAIL because the checker does not exist.

**Step 2: Implement the minimal checker**

Reuse fixture-to-board conversion instead of duplicating search semantics. Keep corpus access optional so CI does not depend on local untracked exports.

**Step 3: Run focused and neighboring regressions**

Run:

```bash
python3 setup.py build_ext --inplace --force
python3 -m pytest tests/test_issue_41_branching_pockets.py tests/test_issue_38_dead_tunnel.py tests/test_issue_36_endgame.py tests/test_search_diagnostics.py -q
tools/run_c_position_eval_tests.sh
tools/run_c_server_tests.sh
python3 -m pytest tests --ignore=tests/training --ignore=tests/test_native_server_equivalence.py -q
```

Expected: all pass, including #33/#36/#38/#39 coverage embedded in the listed suites.

Run the checker against `/home/sergei-scv/temp/playing-battlesnake/exports/zkh-dot_lost_games` at a production-like budget and save only the summary in the PR evidence. Expected: zero instances of the exact post-fix invariant among positions that complete structural proof; `UNKNOWN` cases are reported separately, never counted as safe.

Measure the three representative positions at 100/200/300 ms and compare `root_analysis_elapsed_ms`, total elapsed time, completed depth, and selected move with the baseline above. Expected: the structural prefix stops at its absolute sub-deadline, root proof resolves the small pockets without consuming the scheduled search interval, and no safety assertion depends on deeper minimax completion. The interval schedules a search attempt; it does not promise a completed depth or strict wall-clock completion when one leaf is noninterruptible.

**Step 4: Commit**

```bash
git add tools/check_duel_structural_policy.py tests/test_issue_41_branching_pockets.py README.md docs/native-duel-performance.md
git commit -m "test(search): audit structural root policy"
```

Stage documentation paths only if actually changed.

### Task 4: Final acceptance audit

**Files:**
- Review only: every file changed since `f02f34dd9d27e689af5608da97dfc0e004598ac6`

**Step 1: Inspect scope and history**

```bash
git diff --check origin/main...HEAD
git diff --stat origin/main...HEAD
git log --oneline origin/main..HEAD
git status --short
```

Expected: only issue #41 plan, proof, diagnostics, fixtures/tests, optional checker/docs; no `.so`, `build/`, corpus exports, or unrelated files.

**Step 2: Map every live checkbox to evidence**

Record exact test names/output for all seven acceptance criteria and all three non-goals. Explicitly distinguish proved root cause from corpus correlation, and state any remaining `UNKNOWN` proof cases without calling them safe.

**Step 3: Commit any review-only documentation correction**

If no correction is needed, do not create an empty commit. Otherwise commit only the correction with a precise message.
