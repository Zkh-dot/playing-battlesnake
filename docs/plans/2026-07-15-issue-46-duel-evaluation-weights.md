# Explicit Production Duel Evaluation Weights Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Give the production native duel server a versioned, selectable, auditable evaluation-weight source without changing the production default, and produce a reproducible 100+ game promotion report.

**Architecture:** Versioned JSON envelopes remain the reviewable source of truth. A deterministic generator validates every field and emits immutable C profiles plus content SHA-256 values; the native server selects a compiled profile at startup, rejects unknown selectors, copies its weights into the existing search config, and reports profile identity in startup and per-move diagnostics. Promotion evidence is produced by separate seeded paired-match and replay-risk tools, and this plumbing PR deliberately keeps the current zero-pressure profile as the default.

**Tech Stack:** Python 3.11 standard library, C23 native core/server, CPython extension, pytest/unittest, deterministic code generation, seeded native minimax benchmarks.

---

## Baseline and design record

- Base: `d7755acfca4d699012575913ed9cc2ba1005b68b` (fresh `origin/main` after issue #45).
- Production control flow is proven, not inferred: `server_main.c::config_from_env` builds `BsStrategyConfigDefault`; `battlesnake_strategy.c::BsChooseMove` calls `CoreMinimaxMove`; `CoreMinimaxMove` creates `CoreSearchConfigDefault`; that function installs `CoreEvaluationWeightsDefault`, whose four opponent-pressure terms are zero.
- `BATTLESNAKE_DUEL_WEIGHTS_PATH=configs/evaluation_weights/tuned-opponent-pressure.json` is ignored today. The server starts successfully and prints only `battlesnake native server listening on 127.0.0.1:18346`, with neither profile identity nor content hash.
- At 300 ms on current main, the four issue diagnostics already select `left`, `right`, `left`, and `up` under both default and tuned weights. Fixes #41-#44 therefore made these examples useful diagnostics but removed their value as proof that the tuned profile should be promoted.
- Considered design A: parse an arbitrary JSON file at process startup. It maximizes runtime flexibility, but adds a second JSON parser/contract, startup filesystem dependency, file-size limits, numeric parsing, and deployment-path failure modes to the multithreaded server.
- Selected design B: validate JSON at build/test time and generate a registry of immutable compiled profiles. Startup configuration remains explicit through a profile selector; unknown selection fails before binding the socket. This is the issue's generated-artifact option and has no request-path I/O or shared mutable state.
- The selected profile identifier is `<name>@<version>`. The SHA-256 is over canonical JSON for the validated `weights` mapping, so formatting changes do not masquerade as behavioral changes.
- Promotion gate is declared before running it: no default change in this PR regardless of result; a future promotion PR requires zero search errors, no increase in independently classified structural-policy violations, no material structural-risk-selection increase, non-inferior survival, a positive paired win signal with an explicitly reported uncertainty interval, and p99 move latency within both the 300 ms search budget and 10% of default. A failure or inconclusive interval means “do not promote”.

## Acceptance mapping

| Issue acceptance criterion | Plan coverage |
| --- | --- |
| Explicit documented production source | Tasks 1, 2, and 4: validated JSON envelopes, generated C registry, documented selector/default |
| Name/version/hash in startup/search diagnostics | Task 2: startup event and every `move_request` telemetry record |
| Native/Python parsing and evaluation parity | Task 1: strict shared schema expectations, generated-registry introspection, all-field and evaluation parity tests |
| Invalid config never silently falls back | Tasks 1 and 2: generator rejects malformed/incomplete profiles; server rejects unknown or malformed selector before listen |
| Seeded 100+ A/B plus replay-risk report before default change | Task 3: 100 paired seeds / 200 games, latency/survival/wins/risk metrics, four replay fixtures; Task 4 verifies default unchanged |
| Promotion separated from plumbing | Every task: candidate is selectable, but generated registry and startup default remain `duel-default@1`; report makes a recommendation only |

## Task 1: Establish one versioned profile contract and generated native registry

**Files:**

- Modify: `configs/evaluation_weights/default.json`
- Modify: `configs/evaluation_weights/tuned-opponent-pressure.json`
- Create: `tools/tuning/duel_weight_profiles.py`
- Create: `tools/tuning/generate_duel_weight_profiles.py`
- Create: `battlesnake/c-core/core/duel_weight_profiles_generated.h`
- Create: `battlesnake/c-core/core/duel_weight_profiles_generated.c`
- Modify: `tools/tuning/evaluate_weights.py:111-114`
- Modify: `tools/tuning/search_weights.py` at its output serialization boundary
- Modify: `battlesnake/c-core/py-core/py_core.c` module methods
- Modify: `battlesnake/battlesnake_native.pyi`
- Modify: `setup.py:10-32`
- Modify: `tools/build_native_server.sh:12-38`
- Modify: `tools/run_c_server_tests.sh`
- Create: `tests/c/test_duel_weight_profiles.c`
- Modify: `tests/test_evaluate_weights.py`
- Create: `tests/test_issue_46_duel_weights.py`

### Step 1: Write failing schema tests

Add tests requiring each profile file to be an envelope with exactly:

```json
{
  "schema_version": 1,
  "name": "duel-default",
  "version": "1",
  "status": "production-default",
  "weights": { "terminal_win": 1000000.0 }
}
```

The `weights` object must contain exactly all 20 `CoreEvaluationWeights` fields; values must be JSON numbers but not booleans and must be finite; `name`, `version`, and `status` must be non-empty restricted audit tokens. Reject unknown/missing envelope keys, unknown/missing weight keys, duplicate profile identifiers, non-finite programmatic values, and a registry with anything other than one `production-default` profile.

Test `load_weights()` compatibility through the strict loader, but return only the inner mapping to existing tuning callers. Test that candidate output is written as a complete envelope rather than silently losing metadata.

Run:

```bash
python3 -m pytest -q tests/test_evaluate_weights.py tests/test_issue_46_duel_weights.py
```

Expected: FAIL because current files are flat mappings and no strict loader/generator exists.

### Step 2: Implement the strict Python contract and convert both sources

Implement frozen `DuelWeightProfile` data with identifier and canonical SHA-256 properties. Keep the 20-key ordered schema in one Python constant used by validation and generation. Convert `default.json` to `duel-default@1`, `status=production-default`; convert the tuned artifact to `tuned-opponent-pressure@1`, `status=candidate`. Do not alter any numeric weight value.

`load_weights(path)` must call the strict loader so every tuning command receives the same validated inner mapping. `search_weights.py` must preserve the base profile envelope and write candidate status/name/version explicitly.

Run the focused tests again. Expected: schema/loader tests PASS; generated/native tests still FAIL because artifacts do not exist.

### Step 3: Generate a deterministic C registry

The generator must read the explicit ordered source list, validate it, and emit a header/source with:

- an immutable `CoreDuelWeightProfile` containing `name`, `version`, canonical `sha256`, status, and `CoreEvaluationWeights`;
- lookup by exact `<name>@<version>`;
- lookup of the sole production default;
- count/index access for diagnostics and parity tests.

Emit doubles with round-trip-safe representation and escape audited tokens. Add `--check` mode which regenerates in memory and exits nonzero on drift. Generated files contain a “do not edit” banner and source paths.

Run:

```bash
python3 tools/tuning/generate_duel_weight_profiles.py
python3 tools/tuning/generate_duel_weight_profiles.py --check
```

Expected: both commands exit 0; a second generation produces no diff.

### Step 4: Prove all-field native/Python parity

Compile generated C into both the extension and server. Expose read-only `duel_weight_profiles()` in the extension solely for audit/tests. It returns profile metadata and every generated numeric field.

Add:

- C registry lookup/default/count tests;
- Python equality for profile identifiers, status, canonical hash, exact key set, and every value;
- native evaluation equality on a non-terminal two-snake board between each generated profile mapping and the strict Python-loaded mapping;
- generator `--check` test;
- explicit proof that the compiled production default still has all four opponent-pressure values equal to zero.

Run:

```bash
python3 setup.py build_ext --inplace --force
bash tools/run_c_server_tests.sh
python3 -m pytest -q tests/test_evaluate_weights.py tests/test_issue_46_duel_weights.py tests/test_search_diagnostics.py
```

Expected: PASS. The old tuning tests may need only envelope-aware assertions, never weakened key/value checks.

### Step 5: Commit and self-review

Check generated drift and inspect all numeric values against the pre-change JSON values.

```bash
git diff --check
git status --short
git add configs/evaluation_weights tools/tuning/duel_weight_profiles.py tools/tuning/generate_duel_weight_profiles.py tools/tuning/evaluate_weights.py tools/tuning/search_weights.py battlesnake/c-core/core/duel_weight_profiles_generated.h battlesnake/c-core/core/duel_weight_profiles_generated.c battlesnake/c-core/py-core/py_core.c battlesnake/battlesnake_native.pyi setup.py tools/build_native_server.sh tools/run_c_server_tests.sh tests/c/test_duel_weight_profiles.c tests/test_evaluate_weights.py tests/test_issue_46_duel_weights.py
git commit -m "feat(weights): generate versioned duel profiles"
```

## Task 2: Wire the selected profile into production search and diagnostics

**Files:**

- Modify: `battlesnake/c-core/server/battlesnake_strategy.h`
- Modify: `battlesnake/c-core/server/battlesnake_strategy.c`
- Modify: `battlesnake/c-core/server/server_main.c`
- Modify: `tests/c/test_battlesnake_strategy.c`
- Modify: `tests/test_issue_46_duel_weights.py`
- Modify: `tests/test_issue_45_server_concurrency.py` only where the exhaustive supported-environment and telemetry schemas must recognize the new documented field
- Modify: `tests/test_native_server_equivalence.py` if its startup helper requires the new observable line

### Step 1: Write failing strategy and startup tests

Add C tests proving `BsChooseMove` uses `config.weight_profile->weights`, not `CoreEvaluationWeightsDefault()`. Use a general board where two complete valid profiles select different moves; do not key production behavior to a replay/game ID. Also prove a null config intentionally resolves to the compiled production default.

Add process tests that:

- unset selector starts with `duel-default@1`;
- `BATTLESNAKE_DUEL_WEIGHT_SET=tuned-opponent-pressure@1` starts and observes that exact set;
- empty, malformed, or unknown non-empty selector exits nonzero before the listening line;
- startup output includes name, version, status, and 64-hex SHA-256;
- every recognized `/move` telemetry record includes the same immutable identity/hash, including parse failure/fallback responses where no search runs;
- the exhaustive #45 environment scrub knows the new selector and still rejects ambient leakage.

Run:

```bash
bash tools/run_c_server_tests.sh
python3 -m pytest -q tests/test_issue_46_duel_weights.py tests/test_issue_45_server_concurrency.py tests/test_native_server_equivalence.py
```

Expected: FAIL because strategy ignores profile weights and the server has no selector/metadata.

### Step 2: Pass immutable weights into minimax

Extend `BsStrategyConfig` with a non-owning pointer to an immutable generated profile. `BsStrategyConfigDefault()` uses the generated production default. In duel routing, create `CoreSearchConfigDefault(budget)`, overwrite only `search_config.weights` from the selected profile, and call `CoreMinimaxMoveWithStats`. Preserve the existing structural root policy and every deadline behavior.

All request-local config copies retain the immutable pointer. No worker mutates registry/profile memory.

Run C tests. Expected: PASS, including the pre-existing strategy suite.

### Step 3: Fail fast at startup and add bounded diagnostics

Parse only `BATTLESNAKE_DUEL_WEIGHT_SET`. Unset means the explicitly documented production default. A present empty value, invalid token, or unknown identifier prints a precise error and makes `config_from_env` fail before socket creation. Never fall back after an explicit invalid selection.

Keep the existing human-readable listening substring for deployment probes, append identity/hash, and emit a structured bounded startup event. Extend `move_request` JSON with `weight_set`, `weight_version`, and `weight_sha256`; compiled audit tokens make direct JSON output safe, and the line must remain below `PIPE_BUF`.

Run the focused server tests. Expected: PASS.

### Step 4: Verify production-path equivalence and latency

Build the actual binary, send the same duel payload to default and tuned selectors, and compare each response to Python `minimax_diagnostics` using the corresponding loaded profile at the same budget. Confirm logs name the active set and invalid selectors never reach listen.

```bash
bash tools/build_native_server.sh
python3 -m pytest -q tests/test_issue_46_duel_weights.py tests/test_native_server_equivalence.py
python3 -m pytest -q tests/test_issue_45_server_concurrency.py tests/test_issue_45_benchmark.py
```

Expected: PASS with no #45 queue/telemetry regression.

### Step 5: Commit and self-review

```bash
git diff --check
git add battlesnake/c-core/server/battlesnake_strategy.h battlesnake/c-core/server/battlesnake_strategy.c battlesnake/c-core/server/server_main.c tests/c/test_battlesnake_strategy.c tests/test_issue_46_duel_weights.py tests/test_issue_45_server_concurrency.py tests/test_native_server_equivalence.py
git commit -m "feat(server): select and report duel weights"
```

## Task 3: Produce reproducible paired A/B and replay-risk evidence without promotion

**Files:**

- Modify: `tools/tuning/compare_weights_matches.py`
- Create: `tools/tuning/report_duel_weight_replays.py`
- Create: `tests/fixtures/issue_46_duel_weight_replays.json`
- Modify: `tests/test_compare_weights_matches.py`
- Create or modify: `tests/test_issue_46_duel_weights.py`
- Create: `docs/duel-weight-ab-report.md`
- Create: `docs/evidence/issue-46-duel-weight-ab.json`
- Create: `docs/evidence/issue-46-duel-weight-replays.json`

### Step 1: Freeze experiment semantics in failing tests

Require the match tool to generate `--scenario-count N` boards from a recorded `--seed`, play each board twice with sides swapped, and report settings plus per-profile:

- wins/losses/draws and paired outcomes;
- turns survived, alive-at-cap count/rate, and terminal survival rate;
- move count, search errors/timeouts;
- p50/p95/p99/max native-reported move latency;
- structural-risk selections (capacity-deficient non-safe selection while a safe alternative exists);
- independent structural-policy violations using the existing diagnostics audit.

Tests must show deterministic scenario/side scheduling, correct percentile and metric aggregation from fake diagnostics, and no use of wall-clock timing for reported search latency.

The replay tool must read the committed minimal board fixture for exactly the four live-issue positions, run both named profiles at configured budget/repeats, and report moves, depths, timeouts, selected structural proof, root-comparison reason, risk classification, and policy violations. Tests assert complete records and schema, not universal expected moves.

Run:

```bash
python3 -m pytest -q tests/test_compare_weights_matches.py tests/test_issue_46_duel_weights.py
```

Expected: FAIL because current match tool repeats 20 scenarios with side assignment coupled to index and reports only wins/turns/errors.

### Step 2: Implement paired metrics and replay diagnostics

Reuse `minimax_diagnostics` once per actual move and retain its returned `elapsed_ms` and root candidates. Reuse `tools.check_duel_structural_policy.audit_diagnostics` for independent violations; add a small explicitly tested structural-risk predicate for the issue metric. Never alter move selection based on the audit.

Extract the four source frames read-only from the user's export corpus into a compact board-only fixture containing source game ID, turn, recorded move, snake ID, board dimensions/ruleset, snakes, food, and hazards. Do not copy full games and do not encode a required replacement move.

Run focused tests. Expected: PASS.

### Step 3: Run the predeclared replay-risk experiment

```bash
python3 tools/tuning/report_duel_weight_replays.py \
  --fixture tests/fixtures/issue_46_duel_weight_replays.json \
  --profiles configs/evaluation_weights/default.json configs/evaluation_weights/tuned-opponent-pressure.json \
  --time-budget-ms 300 --repeats 5 \
  --output docs/evidence/issue-46-duel-weight-replays.json
```

Expected: 40 records (4 positions × 2 profiles × 5 repeats), zero errors, explicit timeout/depth/risk data. Report nondeterministic budget-sensitive differences honestly; do not turn a replay move into an assertion.

### Step 4: Run the seeded 200-game paired gate

Run on the preferred compute node if reachable, using an exact clean checkout of the task commit and record its commit, Python/compiler/CPU metadata. The run is 100 generated seeds, each played twice with sides swapped:

```bash
python3 tools/tuning/compare_weights_matches.py \
  --before-weights configs/evaluation_weights/default.json \
  --after-weights configs/evaluation_weights/tuned-opponent-pressure.json \
  --seed 46001 --scenario-count 100 \
  --fixed-depth 3 --time-budget-ms 300 --max-turns 200 \
  --output docs/evidence/issue-46-duel-weight-ab.json \
  --markdown-output /tmp/issue-46-duel-weight-ab.generated.md
```

Expected: exactly 200 matches, 100 observations for each physical side per profile, zero errors, complete metrics. Preserve the raw JSON. If the node is unavailable, run locally and state the host; do not reduce the sample count.

### Step 5: Write the evidence report and make a non-promotion decision

`docs/duel-weight-ab-report.md` must state frozen settings, commit/profile hashes, paired outcomes and uncertainty, survival, latency, structural-risk/violation counts, replay table, limitations, and a clear `promote`, `do not promote`, or `inconclusive` recommendation against the predeclared gate. Regardless of the recommendation, production default remains unchanged in this PR; any promotion is a separately reviewed future change.

Verify evidence consistency with an automated test that recomputes summary counts from raw rows.

### Step 6: Commit and self-review

```bash
git diff --check
git add tools/tuning/compare_weights_matches.py tools/tuning/report_duel_weight_replays.py tests/fixtures/issue_46_duel_weight_replays.json tests/test_compare_weights_matches.py tests/test_issue_46_duel_weights.py docs/duel-weight-ab-report.md docs/evidence/issue-46-duel-weight-ab.json docs/evidence/issue-46-duel-weight-replays.json
git commit -m "test(weights): add duel promotion evidence gate"
```

## Task 4: Document operations and run the complete issue contract

**Files:**

- Modify: `README.md` native-server configuration section
- Modify: `docs/runbooks/battlesnake-deploy.md`
- Modify: `docs/weight-tuning-report.md`
- Modify: `tests/test_issue_46_duel_weights.py`
- Modify only as required by verified failures: adjacent test/build documentation

### Step 1: Add failing documentation contract tests

Require deploy docs to name:

- source envelopes and deterministic generator/check command;
- exact `BATTLESNAKE_DUEL_WEIGHT_SET` semantics;
- explicit unset default `duel-default@1`;
- exact invalid-selector startup failure;
- startup and move telemetry fields;
- candidate status and separate promotion workflow;
- reproducible 200-game and replay-report commands.

Run the focused documentation test. Expected: FAIL before docs change.

### Step 2: Update operator and tuning documentation

Document build-time validation and runtime selection. Do not tell operators to select the candidate as the default. Update the old 20-game report to link the new 200-game evidence and preserve the fact that the old result was directional only.

Run focused tests. Expected: PASS.

### Step 3: Rebuild and run the full risk-proportional suite

```bash
python3 tools/tuning/generate_duel_weight_profiles.py --check
python3 setup.py build_ext --inplace --force
bash tools/build_native_server.sh
bash tools/run_c_server_tests.sh
bash tools/run_c_position_eval_tests.sh
python3 -m pytest -q \
  tests/test_issue_46_duel_weights.py \
  tests/test_evaluate_weights.py \
  tests/test_compare_weights_matches.py \
  tests/test_search_diagnostics.py \
  tests/test_issue_41_branching_pockets.py \
  tests/test_issue_42_root_comparison.py \
  tests/test_issue_43_search_budget_stability.py \
  tests/test_issue_44_corridor_guard.py \
  tests/test_issue_45_server_concurrency.py \
  tests/test_issue_45_benchmark.py \
  tests/test_native_server_equivalence.py \
  tests/test_issue_38_dead_tunnel.py \
  tests/test_issue_36_endgame.py \
  tests/test_issue_27_deadline.py
python3 -m pytest -q
```

Expected: all available tests PASS; host-sensitive performance skips remain explicitly reported. Re-run the #45 scrubbed concurrency/latency acceptance gate with both the default selector and the candidate selector, and require the same zero-failure contract and bounded p99.

### Step 4: Audit acceptance and default immutability

Verify:

- generated registry `production-default` is still `duel-default@1`;
- all four opponent-pressure values in that profile remain zero;
- selecting the candidate requires an explicit environment value;
- malformed source fails generator/build and malformed/unknown selector fails startup;
- startup and each move log carry the profile hash;
- evidence has 200 paired games and all requested measures;
- no full replay/game-ID-specific branch exists in production code;
- build artifacts are ignored and no user export file was modified.

### Step 5: Commit and self-review

```bash
git diff --check
git status --short
git log --oneline origin/main..HEAD
git add README.md docs/runbooks/battlesnake-deploy.md docs/weight-tuning-report.md tests/test_issue_46_duel_weights.py
git commit -m "docs(weights): document duel profile operations"
```

## Final review and branch completion

After every task has separate implementer, spec-compliance, and quality approval, run a fresh whole-diff reviewer against `origin/main`, including all live acceptance criteria and non-goals. Rebase/refresh only through safe non-destructive workflow if `origin/main` moved; rerun generated drift, focused tests, C runners, full pytest, production server selection, replay evidence consistency, and latency gates on the final head.

Then use `finishing-a-development-branch`: preserve the issue branch, push it, create a PR with `Closes #46`, exact root cause, generated-artifact rationale, unchanged default, profile hashes, A/B/replay evidence, test commands, limitations, and risks. Request `ya-yara`, wait for exact-head approval and CI, resolve review via the same implementer → spec → quality loop, merge only when green/approved, verify the merge SHA on `main`, confirm issue closure, and only then clean this worktree.
