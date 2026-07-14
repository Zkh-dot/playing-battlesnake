# Search-Budget-Stable Duel Selection Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make safety-critical duel decisions reproducible under deterministic search work budgets and lock the production 100/200/300 ms behavior to the structural root proof contract.

**Architecture:** Preserve the production wall-clock path when `node_budget == 0`. Add a diagnostics-only deterministic mode that completes the existing resource-bounded structural analysis without a wall deadline, then interrupts iterative minimax at an exact node boundary and publishes only the last complete root iteration (or the existing conservative partial-root bound snapshot when no iteration completed). Keep structural proof ordering, minimax bounds, and budget provenance explicit in diagnostics.

**Tech Stack:** C11 native search core, CPython C extension, Python type stubs, pytest, JSON replay fixtures, Python benchmark CLI

---

## Verified baseline and design decision

- Base: `f88ef93a56da08c7b074e329472c82d6d1c0e8b8` (`origin/main`, after issues #41 and #42).
- Current production behavior already classifies the risky move as structurally dominated and selects the safe root at 100, 200, and 300 ms for replay `0188bbac...` turn 288, `8fd97d0d...` turn 357, and `c7add22b...` turn 278.
- The structurally equivalent turn-169 position legitimately changes from `up` to `left` as completed depth increases; both remain safe. Cross-budget move identity is therefore intentionally not asserted for equivalent maximal roots.
- Missing acceptance coverage: committed 8fd/c7 replay positions, deterministic fixed-node execution, explicit node-cutoff provenance, and a repeatable 100/200/300 ms plus fixed-node benchmark report.
- Rejected: increasing the production timeout, because it does not establish a safety invariant.
- Rejected: using fixed depth as the deterministic budget, because work grows nonlinearly and cannot model a bounded search budget.
- Rejected: combining structural-state visits and minimax nodes in one counter, because they are semantically different work units and doing so would couple proof implementation details to minimax reproducibility.
- Selected: `node_budget` applies only to minimax nodes; in this diagnostic mode wall time cannot choose a move, while the structural phase uses its existing board-derived resource/horizon caps and analyzes every root before deepening.

## Task 1: Add a deterministic node-budget search contract

**Acceptance criteria:** fixed-node runs are repeatable; the cap never publishes a partial iteration as exact; diagnostics distinguish wall timeout from node exhaustion; default production behavior is unchanged.

**Files:**

- Modify: `tests/test_search_diagnostics.py:195-229, 797-801`
- Modify: `battlesnake/c-core/core/search_stats.h:68-78, 115-171, 199-208`
- Modify: `battlesnake/c-core/core/search_stats.c:30-69`
- Modify: `battlesnake/c-core/core/core_algorithms.c:1214-1278, 5330-5351, 5480-5580, 5820-5867, 5942-6141`
- Modify: `battlesnake/c-core/py-core/py_core.c:506-515, 696-809`
- Modify: `battlesnake/battlesnake_native.pyi:94-128, 141-152`

### Step 1: Write the failing Python API/diagnostics tests

Add tests that call `minimax_diagnostics(..., node_budget=...)` and assert:

- negative values raise `ValueError`;
- `node_budget=0` preserves wall-clock mode (`node_budget_exhausted == False`);
- a small positive budget reports the configured budget, `nodes <= node_budget`, `node_budget_exhausted == True`, and `timed_out == False`;
- five repeats with the same board and node cap produce the same move, completed/max depth, node count, root scores/bounds, structural proof/cutoff, comparison reason, and selection reason;
- when exhaustion interrupts a deeper iteration, the returned depth/root records equal a separate fixed-depth run at `completed_depth`; no root from the partial iteration is presented as the completed snapshot.

Run:

```bash
pytest -q tests/test_search_diagnostics.py -k 'node_budget'
```

Expected: FAIL because `node_budget` and its diagnostics do not exist.

### Step 2: Add the core configuration and provenance fields

Add `uint64_t node_budget` to `CoreSearchConfig`, defaulting to zero. Add `uint64_t node_budget` and `bool node_budget_exhausted` to `CoreSearchStats`. Add `CORE_SELECTION_NODE_BUDGET_BEST_SO_FAR` so diagnostics do not label deterministic exhaustion as a wall timeout.

Keep `CoreMinimaxMove()` and production callers on the zero default.

### Step 3: Centralize interruption semantics

In `core_algorithms.c`, replace direct recursive/loop timer checks with one helper that:

1. in node mode, stops before incrementing when `stats->nodes >= config.node_budget`, marks `node_budget_exhausted`, and never consults wall time;
2. otherwise preserves the existing monotonic-deadline behavior.

Use a general `interrupted` flag internally. On an interrupted iteration, retain the last complete `CoreRootIterationSnapshot`; only use the already bound-aware partial-root fallback if no depth completed. Finalize its selection source as `node_budget_best_so_far` or `timeout_best_so_far` according to the actual cutoff. Set `stats.timed_out` only for a wall cutoff.

In node mode, call the existing structural root profiler with deadline enforcement disabled. Its capacity/cycle/horizon/resource caps still bound every root, and every allowed root receives a proof result before minimax starts.

### Step 4: Expose the diagnostics-only Python option

Append `node_budget` to the keyword arguments of `minimax_diagnostics` so existing positional calls keep their meaning. Validate that it fits `uint64_t`, copy it into `CoreSearchConfig`, and return `node_budget` plus `node_budget_exhausted`. Update `selection_reason_name`, `MinimaxDiagnostics`, and the function signature in the `.pyi` file.

Do not add `node_budget` to `minimax_move`; production remains wall-clock configured.

### Step 5: Verify focused and adjacent behavior

Run:

```bash
python3 setup.py build_ext --inplace --force
pytest -q tests/test_search_diagnostics.py -k 'node_budget or minimax_diagnostics_shape or fixed_depth'
pytest -q tests/test_issue_27_deadline.py tests/test_issue_41_branching_pockets.py tests/test_issue_42_root_comparison.py
```

Expected: PASS. Existing deadline tests must retain their prior semantics in default mode.

### Step 6: Commit

```bash
git add battlesnake/c-core/core/search_stats.h battlesnake/c-core/core/search_stats.c battlesnake/c-core/core/core_algorithms.c battlesnake/c-core/py-core/py_core.c battlesnake/battlesnake_native.pyi tests/test_search_diagnostics.py
git commit -m "feat: add deterministic minimax node budgets"
```

## Task 2: Lock replay safety and complete-root publication

**Acceptance criteria:** every allowed root has a bounded structural result before deepening; the three risky replay roots remain rejected at 100/200/300 ms and deterministic fixed-node budgets; deeper search may change only between structurally equivalent maximal candidates; equal proof/depth inputs are repeatable.

**Files:**

- Create: `tests/fixtures/issue_43_search_budget_positions.json`
- Create: `tests/test_issue_43_search_budget_stability.py`
- Read-only source for fixture extraction: `/home/sergei-scv/temp/playing-battlesnake/exports/zkh-dot_lost_games/*.json`

### Step 1: Build minimal committed fixtures

Extract only the board state, controlled snake id, replay/game id, turn, risky move, expected selected safety class, and source metadata for:

- `0188bbac...`, turn 288;
- latest risky `8fd97d0d...`, turn 357;
- latest risky `c7add22b...`, turn 278;
- the turn-169 structurally equivalent-candidate case already represented in the issue-41 fixture.

Do not commit complete replay exports or generated native artifacts.

### Step 2: Write regression tests and prove baseline scope

For each risky replay position, at 100/200/300 ms assert:

- all board-safe root commands are `evaluated` and have a non-`not_analyzed` structural proof;
- the recorded risky move is `unsafe` or `unknown` and is structurally dominated when a `safe` alternative exists;
- the selected move belongs to the global structurally maximal frontier and is not the risky move;
- completed/max depth, bound, proof/cutoff, root comparison reason, and selection reason are present and coherent.

For node budgets selected from measured 100/200/300 ms node ranges, run each position three times and assert the safety classification and all decision-relevant diagnostics are identical. Assert only safety/frontier invariants—not identical moves—on the equivalent turn-169 case, and demonstrate at least two budgets with different completed depths.

Before implementation, run these tests in a detached worktree at `f88ef93`. Expected: wall-time safety assertions PASS (evidence that #41/#42 already fixed that portion); fixed-node calls FAIL with `TypeError`. Record both facts in the PR rather than claiming all tests were red.

Run on the issue branch:

```bash
pytest -q tests/test_issue_43_search_budget_stability.py
```

Expected: PASS.

### Step 3: Commit

```bash
git add tests/fixtures/issue_43_search_budget_positions.json tests/test_issue_43_search_budget_stability.py
git commit -m "test: cover duel search budget stability"
```

## Task 3: Add the required reproducible budget benchmark

**Acceptance criteria:** report move, safety proof, completed/max depth, bound, nodes, and latency for 100/200/300 ms and deterministic node budgets; benchmark output explains why a move changes.

**Files:**

- Create: `benchmarks/bench_issue_43_search_budgets.py`
- Create: `tests/test_issue_43_benchmark.py`
- Read: `benchmarks/run_minimax_bench.py`
- Read: `tests/fixtures/issue_43_search_budget_positions.json`

### Step 1: Write a failing CLI smoke test

Specify a CLI that accepts `--repeats`, optional repeated `--time-budget-ms`, optional repeated `--node-budget`, and `--json`. The test runs one repeat with small budgets and asserts each record contains position id, budget kind/value, repeat, move, structural proof/cutoff, completed/max depth, selected bound/outcome, comparison/selection reason, nodes, root-analysis nodes, and elapsed milliseconds.

Run:

```bash
pytest -q tests/test_issue_43_benchmark.py
```

Expected: FAIL because the benchmark module does not exist.

### Step 2: Implement the benchmark without pass/fail tuning

Load only the committed fixtures. Default to 100/200/300 ms and fixed-node budgets chosen as rounded workload points spanning the observed depths, not as promoted gameplay weights or safety thresholds. Emit one JSON object per run in `--json` mode and a compact human table otherwise. Include the selected candidate's proof/bound and both comparison reasons so equivalent-candidate changes are explainable.

The benchmark records measurements; it must not encode a required move for structurally equivalent candidates or modify production configuration.

### Step 3: Run and capture acceptance evidence

```bash
pytest -q tests/test_issue_43_benchmark.py
python3 -m benchmarks.bench_issue_43_search_budgets --repeats 3 --json
```

Expected: tests PASS; JSON lines cover all four fixtures and all default budgets. Summarize depth/nodes/latency ranges in the PR.

### Step 4: Commit

```bash
git add benchmarks/bench_issue_43_search_budgets.py tests/test_issue_43_benchmark.py
git commit -m "bench: report duel search budget stability"
```

## Final issue validation

### Step 1: Rebuild the production extension

```bash
python3 setup.py build_ext --inplace --force
```

Expected: successful warning-free build.

### Step 2: Run focused, predecessor, native, and broad suites

```bash
pytest -q tests/test_issue_43_search_budget_stability.py tests/test_issue_43_benchmark.py tests/test_search_diagnostics.py
pytest -q tests/test_issue_27_deadline.py tests/test_issue_33_endgame.py tests/test_issue_36_endgame.py tests/test_issue_38_dead_tunnel.py tests/test_issue_39_duel_structure.py tests/test_issue_41_branching_pockets.py tests/test_issue_42_root_comparison.py tests/test_duel_structural_policy_checker.py
pytest -q tests/test_native_server_equivalence.py tests/test_native_vs_python.py
pytest -q
```

If a named historical file has been renamed, use `rg --files tests` to identify the current corresponding suite and record the substitution. Expected: PASS except pre-existing skips documented verbatim.

### Step 3: Run C/native tests and benchmark

```bash
tools/run_c_position_eval_tests.sh
tools/run_c_server_tests.sh
python3 -m benchmarks.bench_issue_43_search_budgets --repeats 3 --json
```

Expected: both C runners PASS; every risky position remains outside the selected frontier at all time/node budgets; repeated fixed-node records are decision-identical.

### Step 4: Audit acceptance criteria and repository hygiene

Verify:

- the structural phase writes a result for every allowed root before minimax;
- wall expiry cannot promote a lower safety class;
- fixed-node same-input runs are decision-identical;
- incomplete iterations never overwrite the completed snapshot or masquerade as exact;
- diagnostics expose depth, max depth, bound, proof/cutoff, comparison/selection reason, and cutoff type;
- benchmark reports depth/nodes/latency at 100/200/300 ms;
- no timeout increase, replay-specific production branch, magic safety threshold, or equivalent-root move-identity assertion was added.

Run:

```bash
git diff origin/main...HEAD --check
git status --short
git log --oneline origin/main..HEAD
```

Expected: clean diff, only intended commits/files, no build artifacts.

### Step 5: Final review and PR workflow

Run the required final spec and code-quality reviews against `origin/main...HEAD`, then use `finishing-a-development-branch` with the PR-preserving option. Push `issue-43-search-budget-stability`, open a PR containing `Closes #43`, request repository review, resolve every substantive thread through the implementer/reviewer loop, wait for green CI and approval, merge per repository policy, verify the merge SHA on `origin/main` and issue closure, then remove only this issue's worktree/local branch.
