# Native Move Concurrency Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Serve overlapping native `/move` requests within their individual 500 ms deadlines using bounded concurrency, request-age-aware fallback, and observable overload behavior.

**Architecture:** Keep the existing native HTTP/search implementation, but move accepted sockets into a fixed-size pthread worker pool backed by a bounded FIFO. Every job carries its monotonic accept timestamp; request parsing and search use the elapsed request age to decide whether normal search still fits, otherwise returning the existing legal request-local fallback. The accept loop never creates unbounded threads, and hard queue saturation is rejected promptly while queued worker saturation remains capable of returning a legal fallback.

**Tech Stack:** C23/POSIX sockets and pthreads, existing native arena/parser/search core, Python `pytest` HTTP integration tests, Python stdlib load benchmark, GCC ThreadSanitizer/strict-warning builds.

---

## Established baseline and design choice

- Fresh base: `origin/main` at `f2ee9934613933811fb399dff54feadc1b52beec`.
- Current `server_main.c` performs `accept -> read -> parse -> search -> write -> close` serially.
- Two simultaneous `duel_center_pressure_11x11` requests with a 400 ms configured search budget and 500 ms game timeout reproduced the defect: one response completed in `350.6 ms`; the other timed out at `500.9 ms`. The undrained serial backlog made later pairs time out at about `501 ms` each.
- The three issue positions were exported from the live replays and independently exercised through the current native HTTP path: T292 returned `right` in `355.1 ms`, T3 returned `down` in `350.6 ms`, and T58 returned `up` in `351.3 ms`.
- A control using two isolated native processes released simultaneously returned both searches in `350.8 ms`, so parallel execution fits the deadline on this host and the serial accept loop is the proven local cause.
- Thread-safety audit: JSON parser structs and arenas are request-local; each parsed `Board` is heap-owned by its request; `CoreSearchContext` owns the transposition table, workspace, search state, timer, stats, PV/killer/history arrays; space-time scratch is `_Thread_local`; production position-eval test controls are excluded without test macros; server configuration is immutable after startup. The only process-global production mutation is the signal-safe shutdown flag.
- Alternatives considered:
  - Multiple process replicas behind a proxy isolate state but require deployment/proxy changes, leave kernel/proxy queue age outside the application telemetry, and duplicate transient TT memory without improving the two-request CPU result.
  - Thread-per-connection is small but unbounded, allowing a burst to allocate an unbounded number of request buffers and per-search transposition tables.
  - A fixed pthread pool plus bounded FIFO is the smallest repo-local design that provides explicit resource limits, measured queue age, and deterministic backpressure.

## File map

- Create `battlesnake/c-core/server/connection_queue.h`: bounded FIFO job and lifecycle API; no HTTP/search knowledge.
- Create `battlesnake/c-core/server/connection_queue.c`: mutex/condition-variable implementation.
- Modify `battlesnake/c-core/server/server_main.c`: immutable concurrency configuration, worker lifecycle, timestamped jobs, telemetry, and hard-cap response.
- Modify `battlesnake/c-core/server/battlesnake_http.h`: timed request context and move-result telemetry fields.
- Modify `battlesnake/c-core/server/battlesnake_http.c`: subtract request age before search and expose fallback outcome.
- Modify `battlesnake/c-core/server/battlesnake_strategy.h`: public cheap fallback and search-window predicate.
- Modify `battlesnake/c-core/server/battlesnake_strategy.c`: principled early fallback when less than the configured minimum search window remains.
- Modify `tools/build_native_server.sh`: compile the queue and link pthreads.
- Modify `tools/run_c_server_tests.sh`: build queue tests and link affected server tests with pthreads.
- Create `tests/c/test_connection_queue.c`: deterministic bounded/FIFO/stop contract.
- Modify `tests/c/test_battlesnake_strategy.c`: insufficient-time fallback contract.
- Modify `tests/c/test_battlesnake_http.c`: deterministic request-age accounting and metadata.
- Create `tests/fixtures/issue_45_timeout_replay_positions.json`: the three live replay positions and exact expected safe moves.
- Create `tests/test_issue_45_server_concurrency.py`: production binary concurrency, queued fallback, hard-cap, telemetry, and replay tests.
- Create `benchmarks/bench_issue_45_server_concurrency.py`: configurable concurrent load runner and p99/safety-margin report.
- Create `tests/test_issue_45_benchmark.py`: benchmark aggregation and failure-gate tests without sockets.
- Modify `docs/runbooks/battlesnake-deploy.md`: bounded model, configuration, telemetry, capacity and load-test procedure.

### Task 1: Make the per-request search decision account for elapsed request age

**Files:**
- Modify: `battlesnake/c-core/server/battlesnake_strategy.h:1-24`
- Modify: `battlesnake/c-core/server/battlesnake_strategy.c:6-92`
- Modify: `battlesnake/c-core/server/battlesnake_http.h:1-39`
- Modify: `battlesnake/c-core/server/battlesnake_http.c:390-535`
- Modify: `tests/c/test_battlesnake_strategy.c`
- Modify: `tests/c/test_battlesnake_http.c`

- [ ] **Step 1: Add failing strategy tests for the explicit search-window boundary**

Add tests which build a valid duel board and assert:

```c
BsStrategyConfig config = BsStrategyConfigDefault();
config.game_timeout_ms = 500;
config.safety_margin_ms = 150;
config.min_time_budget_ms = 50;
assert(BsStrategyHasSearchWindow(&config, 299));
assert(!BsStrategyHasSearchWindow(&config, 301));

MoveDirection move = MOVE_INVALID;
assert(BsChooseFallbackMove(board, "me", &move) == BS_STRATEGY_FALLBACK_USED);
assert(move >= MOVE_UP && move <= MOVE_RIGHT);
```

The predicate is derived only from configured values:
`game_timeout_ms - elapsed_ms - safety_margin_ms >= min_time_budget_ms`.

- [ ] **Step 2: Run the strategy test and verify RED**

Run: `tools/run_c_server_tests.sh`

Expected: compile failure because `BsStrategyHasSearchWindow` and `BsChooseFallbackMove` are not public yet.

- [ ] **Step 3: Expose the cheap fallback and search-window predicate**

Add to `battlesnake_strategy.h`:

```c
#include <stdbool.h>

bool BsStrategyHasSearchWindow(const BsStrategyConfig* config, int elapsed_ms);
BsStrategyStatus BsChooseFallbackMove(
    const Board* board,
    const char* snake_id,
    MoveDirection* out_move
);
```

Rename the existing private `fallback_move` implementation to the public function. Implement the predicate with saturating integer subtraction and no hidden threshold. Keep `BsStrategyEffectiveBudgetMs()` unchanged for callers that already provide a remaining game timeout; the HTTP layer will skip it when the minimum window is unavailable.

- [ ] **Step 4: Add failing HTTP tests for request-age accounting**

Extend the public result/context types:

```c
typedef struct {
    int elapsed_before_handle_ms;
} BsHttpRequestContext;

typedef struct {
    int status_code;
    size_t response_len;
    bool is_move;
    bool fallback_used;
    int game_timeout_ms;
    int elapsed_before_search_ms;
} BsHttpResult;
```

Test a 500 ms payload twice through the real parser. With `elapsed_before_handle_ms = 0`, expect a legal normal response and `fallback_used == false`. With `elapsed_before_handle_ms = 301`, expect a legal 200 response, `is_move == true`, `fallback_used == true`, and elapsed metadata at least 301 ms. The exact move assertion must be legal/safe, not tied to a game ID.

- [ ] **Step 5: Run the HTTP test and verify RED**

Run: `tools/run_c_server_tests.sh`

Expected: compile failure because timed handling and result metadata do not exist.

- [ ] **Step 6: Implement timed request handling minimally**

Add this API while preserving the old wrapper for existing callers/tests:

```c
BsHttpResult BsHandleHttpRequestTimed(
    const char* request,
    size_t request_len,
    BsArena* arena,
    const BsStrategyConfig* strategy_config,
    const BsHttpRequestContext* request_context,
    char* response,
    size_t response_capacity
);
```

`BsHandleHttpRequest()` calls the timed form with `NULL`. The timed form starts a monotonic clock on entry, parses exactly once, rounds elapsed parsing time upward to integer milliseconds, and computes total age as `elapsed_before_handle_ms + parse_elapsed_ms` with saturation at `INT_MAX`. For `/move`, copy the immutable strategy config per request, preserve the payload timeout in result metadata, and either:

1. call `BsChooseFallbackMove()` when `BsStrategyHasSearchWindow()` is false; or
2. set `request_strategy_config.game_timeout_ms = max(1, payload_timeout - total_age)` and call `BsChooseMove()`.

Every response constructor initializes all metadata; non-`/move` routes retain zero/false metadata. Do not add a replay-specific choice or change normal search quality.

- [ ] **Step 7: Verify GREEN and regressions**

Run:

```bash
tools/run_c_server_tests.sh
python3 setup.py build_ext --inplace --force
python3 -m pytest -q tests/test_issue_27_deadline.py tests/test_native_server_equivalence.py
```

Expected: all pass; localhost tests may require socket permission. Existing #27 budget behavior stays green.

- [ ] **Step 8: Commit**

```bash
git add battlesnake/c-core/server/battlesnake_strategy.h \
  battlesnake/c-core/server/battlesnake_strategy.c \
  battlesnake/c-core/server/battlesnake_http.h \
  battlesnake/c-core/server/battlesnake_http.c \
  tests/c/test_battlesnake_strategy.c tests/c/test_battlesnake_http.c
git commit -m "fix(server): account for request age before search"
```

### Task 2: Add the bounded worker pool and production telemetry

**Files:**
- Create: `battlesnake/c-core/server/connection_queue.h`
- Create: `battlesnake/c-core/server/connection_queue.c`
- Create: `tests/c/test_connection_queue.c`
- Modify: `battlesnake/c-core/server/server_main.c:18-239`
- Modify: `tools/build_native_server.sh:4-30`
- Modify: `tools/run_c_server_tests.sh`

- [ ] **Step 1: Write the failing bounded-queue C test**

Define the production-facing contract in `connection_queue.h`:

```c
typedef struct {
    int client_fd;
    struct timespec accepted_at;
} BsConnectionJob;

bool BsConnectionQueueInit(BsConnectionQueue* queue, size_t capacity);
void BsConnectionQueueStop(BsConnectionQueue* queue);
void BsConnectionQueueDestroy(BsConnectionQueue* queue);
bool BsConnectionQueueTryPush(BsConnectionQueue* queue, BsConnectionJob job);
bool BsConnectionQueuePop(BsConnectionQueue* queue, BsConnectionJob* out_job);
```

The test asserts FIFO order, `TryPush` returns false without blocking at capacity, a blocked pop wakes after a push, and `Stop` wakes a blocked pop and returns false once queued jobs drain.

- [ ] **Step 2: Run and verify RED**

Run: `tools/run_c_server_tests.sh`

Expected: compile failure because the queue implementation is absent.

- [ ] **Step 3: Implement the queue with fixed allocation and pthread synchronization**

Use one startup `calloc(capacity, sizeof(BsConnectionJob))`, a mutex, and one non-empty condition variable. `TryPush` never waits. `Pop` waits only while empty and not stopped; after stop it drains existing jobs, then returns false. No worker or request allocates queue nodes.

- [ ] **Step 4: Verify the queue test passes under repetition**

Run:

```bash
tools/run_c_server_tests.sh
for run in 1 2 3 4 5; do build/tests/test_connection_queue; done
```

Expected: every run passes without hang.

- [ ] **Step 5: Write the failing HTTP concurrency test before changing the server loop**

Create the initial portion of `tests/test_issue_45_server_concurrency.py` with a barrier that releases two raw-socket `/move` calls using `duel_center_pressure_11x11`, payload timeout 500 ms, and server search budget 400 ms. Start the production binary with `BATTLESNAKE_WORKERS=2`. Each client uses a 500 ms external socket deadline. Assert both return status 200, a legal move, and elapsed time below 500 ms.

Run: `python3 -m pytest -q tests/test_issue_45_server_concurrency.py -k simultaneous`

Expected on the serial server: FAIL because one request times out; this is the automated form of the measured baseline.

- [ ] **Step 6: Replace the serial accept loop with a fixed worker pool**

Extend immutable `BsServerConfig` with:

```c
int worker_count;       /* BATTLESNAKE_WORKERS, default 2, range 1..64 */
size_t queue_capacity; /* BATTLESNAKE_QUEUE_CAPACITY, default 8, minimum 1 */
```

At startup initialize the queue and exactly `worker_count` pthreads. The accept loop timestamps every accepted socket with `CLOCK_MONOTONIC` and calls `TryPush`. Each worker:

1. pops one job;
2. records worker-start time and queue duration;
3. reads the complete request into request-local buffers/arena;
4. computes accept-to-handler age and calls `BsHandleHttpRequestTimed`;
5. writes/closes the response;
6. records handler and total duration.

If `TryPush` reports the hard queue cap, return a complete HTTP 503 JSON response immediately and close the socket; never silently leave it in the kernel backlog. Signal handling only sets `g_should_stop`; normal shutdown stops/drains the queue, joins every created worker, destroys synchronization primitives, and frees the startup arrays.

- [ ] **Step 7: Emit one structured telemetry line per move**

Write a single `stderr` line after the response attempt, serialized with `flockfile(stderr)`/`funlockfile(stderr)` so concurrent lines cannot interleave:

```json
{"event":"move_request","status":200,"queue_ms":0.2,"handler_ms":350.4,"total_ms":350.7,"timeout_ms":500,"fallback":false}
```

Use measured values, not inferred search budget. Hard-cap responses emit `{"event":"server_overload","status":503}`. No game ID or board state is logged.

- [ ] **Step 8: Update build/test linkage and verify GREEN**

Add `connection_queue.c` and `-pthread` to the native build. Add the new queue runner and pthread linkage to `run_c_server_tests.sh`. In both scripts, parse optional `CFLAGS` into a shell array with `read -r -a extra_cflags <<< "${CFLAGS:-}"` and place `"${extra_cflags[@]}"` in each compile invocation; this gives the race-verification phase a real instrumented binary instead of an ignored environment variable. Do not replace the required `-std=c2x`/POSIX flags.

Run:

```bash
tools/run_c_server_tests.sh
bash tools/build_native_server.sh
python3 -m pytest -q tests/test_issue_45_server_concurrency.py -k simultaneous
```

Expected: C tests pass and both simultaneous HTTP requests return in roughly one search interval, each below 500 ms.

- [ ] **Step 9: Commit**

```bash
git add battlesnake/c-core/server/connection_queue.h \
  battlesnake/c-core/server/connection_queue.c \
  battlesnake/c-core/server/server_main.c \
  tests/c/test_connection_queue.c tests/test_issue_45_server_concurrency.py \
  tools/build_native_server.sh tools/run_c_server_tests.sh
git commit -m "feat(server): serve moves with bounded workers"
```

### Task 3: Cover queued overload and the three replay positions through production HTTP

**Files:**
- Create: `tests/fixtures/issue_45_timeout_replay_positions.json`
- Modify: `tests/test_issue_45_server_concurrency.py`

- [ ] **Step 1: Add the three provenance-preserving replay fixtures**

Store full Battlesnake API payloads for:

```text
0850f349-418a-48c1-893f-588feb989452 T292 -> right
174f9563-b757-4086-b42b-70f7748235c5 T3 -> down
74cb48e8-bcd5-4c61-975d-68b97b14e1f7 T58 -> up
```

Each record includes `game_id`, `turn`, `expected_move`, `previous_direction`, and `payload`. The payload must contain the complete live bodies, food, hazards, dimensions, ruleset, timeout, and our snake ID from the exported frame. Do not retain later-frame outcome data in the production request.

- [ ] **Step 2: Write replay HTTP tests**

For every fixture, call the real `build/battlesnake-server` `/move` endpoint, assert status 200, exact expected move, move differs from the repeated wall direction, next head remains in bounds, and elapsed is below the payload timeout.

- [ ] **Step 3: Write queued-overload and hard-cap tests**

Queued overload test: run with one worker and queue capacity two; release two 350 ms production-like requests simultaneously. Assert both return legal 200 responses before 500 ms. Parse telemetry and assert exactly one request reports a positive queue delay and `fallback=true`; this proves the second request used remaining deadline rather than starting another full search after waiting.

Hard-cap test: run with one worker and queue capacity one, occupy the worker and queue with deliberately incomplete HTTP requests, then send a third complete request. Assert the third receives HTTP 503 promptly (well below the I/O timeout), and the process remains healthy after the blockers close. This verifies bounded memory/backpressure without claiming a legal move beyond the configured hard capacity.

- [ ] **Step 4: Run tests and fix only general defects**

Run:

```bash
python3 -m pytest -q tests/test_issue_45_server_concurrency.py
python3 -m pytest -q tests/test_native_server_equivalence.py tests/test_issue_27_deadline.py
```

Expected: every concurrency/replay/deadline test passes. Timing assertions use the external 500 ms contract, not a machine-specific exact duration.

- [ ] **Step 5: Commit**

```bash
git add tests/fixtures/issue_45_timeout_replay_positions.json \
  tests/test_issue_45_server_concurrency.py
git commit -m "test(server): cover overload and timeout replays"
```

### Task 4: Add the capacity benchmark, safety gate, and deployment documentation

**Files:**
- Create: `benchmarks/bench_issue_45_server_concurrency.py`
- Create: `tests/test_issue_45_benchmark.py`
- Modify: `docs/runbooks/battlesnake-deploy.md`

- [ ] **Step 1: Write failing pure benchmark-summary tests**

Test a public `summarize(samples, deadline_ms)` helper with deterministic samples. It must return request count, timeout/error count, p50, p95, p99, max, deadline and `safety_margin_ms = deadline_ms - p99_ms`. Empty samples and any timeout are explicit failures; p99 at or above the deadline fails the gate.

Run: `python3 -m pytest -q tests/test_issue_45_benchmark.py`

Expected: import failure because the benchmark module does not exist.

- [ ] **Step 2: Implement the concurrent load runner**

CLI options:

```text
--workers 2
--queue-capacity 8
--concurrency 2
--batches 20
--deadline-ms 500
--search-budget-ms 400
--output <optional JSON path>
```

Build/start the production server, warm it once, release each batch with a barrier, apply the external deadline to every socket, capture stderr telemetry, and print/write one JSON document containing latency summary plus server-observed queue/handler/total p99, fallback count, 503 count and timeout count. Exit nonzero for timeout/error/503 at intended concurrency or p99 at/above the deadline. The benchmark does not promote a larger worker count automatically.

- [ ] **Step 3: Verify the summary tests and run the production load gate**

Run:

```bash
python3 -m pytest -q tests/test_issue_45_benchmark.py
python3 benchmarks/bench_issue_45_server_concurrency.py \
  --workers 2 --queue-capacity 8 --concurrency 2 --batches 20 \
  --deadline-ms 500 --search-budget-ms 400 \
  --output /tmp/issue45-concurrency-benchmark.json
```

Expected: 40/40 legal responses, zero timeout/error/503, external and server-total p99 below 500 ms, and a positive documented safety margin. Record the actual values in the runbook; do not invent expected p99.

- [ ] **Step 4: Document model and operational limits**

In `docs/runbooks/battlesnake-deploy.md`, document:

- `BATTLESNAKE_WORKERS=2` and `BATTLESNAKE_QUEUE_CAPACITY=8` defaults;
- bounded resources: two concurrent searches, eight waiting sockets, then prompt 503;
- queued requests carry accept time and use cheap legal fallback when the configured minimum search window no longer fits;
- parser, board, arena, response, timer, stats, TT, workspace and search state ownership; `_Thread_local` scratch;
- telemetry field meanings and a sample line;
- exact benchmark command/result and p99 safety margin;
- how to raise capacity only after repeating load and memory checks.

- [ ] **Step 5: Run race, warning, integration, and adjacent regression checks**

Run:

```bash
CC=gcc CFLAGS='-O1 -g -fsanitize=thread' tools/run_c_server_tests.sh
CFLAGS='-O1 -g -fsanitize=thread' bash tools/build_native_server.sh
python3 -m pytest -q tests/test_issue_45_server_concurrency.py
gcc -std=c2x -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror \
  -Ibattlesnake/c-core \
  battlesnake/c-core/datatypes/coord.c battlesnake/c-core/datatypes/snake.c \
  battlesnake/c-core/datatypes/board.c battlesnake/c-core/core/core_algorithms.c \
  battlesnake/c-core/core/standard_ffa.c battlesnake/c-core/core/position_eval.c \
  battlesnake/c-core/core/search_stats.c battlesnake/c-core/core/search_workspace.c \
  battlesnake/c-core/core/search_state.c battlesnake/c-core/core/zobrist.c \
  battlesnake/c-core/core/transposition_table.c battlesnake/c-core/server/arena.c \
  battlesnake/c-core/server/battlesnake_json.c \
  battlesnake/c-core/server/battlesnake_strategy.c \
  battlesnake/c-core/server/battlesnake_http.c \
  battlesnake/c-core/server/connection_queue.c \
  battlesnake/c-core/server/server_main.c -pthread -lm -o /tmp/issue45-server-strict
python3 setup.py build_ext --inplace --force
tools/run_c_server_tests.sh
python3 -m pytest -q \
  tests/test_issue_27_deadline.py \
  tests/test_native_server_equivalence.py \
  tests/test_issue_41_branching_pockets.py \
  tests/test_issue_42_root_comparison.py \
  tests/test_issue_43_search_budget_stability.py \
  tests/test_issue_44_corridor_guard.py \
  tests/test_issue_45_server_concurrency.py \
  tests/test_issue_45_benchmark.py
```

If the local ThreadSanitizer runtime is unsupported, record the exact environmental error and run a normal stress repetition plus strict-warning build; do not suppress a reported data race.

- [ ] **Step 6: Commit**

```bash
git add benchmarks/bench_issue_45_server_concurrency.py \
  tests/test_issue_45_benchmark.py docs/runbooks/battlesnake-deploy.md
git commit -m "bench(server): gate concurrent move latency"
```

## Final verification and acceptance mapping

- Two simultaneous requests before 500 ms: Task 2 HTTP barrier test and Task 4 load gate.
- Documented bounded concurrency: Task 2 fixed workers/FIFO and Task 4 runbook.
- Queue delay included in deadlines: Task 1 timed handler plus Task 2 accept timestamp/telemetry.
- Request-local/thread-safe state: established audit, Task 4 documentation, ThreadSanitizer/stress verification.
- Legal cheap overload fallback: Task 1 predicate/fallback and Task 3 one-worker queued-overload test. The separate hard-cap 503 is explicit backpressure after the configured bounded queue is exhausted.
- Load p99 and safety margin: Task 4 benchmark and recorded result.
- Three safe replay moves through real HTTP: Task 3 fixtures/tests.
- Non-goal preserved: default per-request search remains 400 ms capped by payload deadline; no global quality reduction and no timeout increase.

Before PR, run `git diff --check`, inspect `git status --short`, verify only planned files/commits are present, and rerun the full non-training suite plus socket tests in an environment allowed to bind localhost.
