# Make Unmake And Final Benchmark Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Finish the remaining minimax performance work by replacing board-clone recursion with correct make/unmake state mutation, then publish final benchmark guardrails.

**Architecture:** Start from the current Task 7 state: diagnostics, TT, recursive ordering, and per-ply workspace are already implemented. Add a `CoreSearchState` module that owns a mutable board plus undo records, route recursive search through it only when `enable_make_unmake` is true, and preserve the clone path as an equivalence oracle. Finish with benchmark documentation and a final local result artifact.

**Tech Stack:** C2x native extension, CPython wrapper, `unittest`, Python benchmark runner, JSONL benchmark artifacts.

---

## File Structure

- `battlesnake/c-core/core/search_state.h`: public search-state API used only by core search.
- `battlesnake/c-core/core/search_state.c`: mutable board copy, undo-stack records, make/unmake implementation, and helper functions mirroring `BoardCloneAndApply()` semantics.
- `battlesnake/c-core/core/core_algorithms.c`: owns `CoreSearchState` in `CoreSearchContext` and chooses clone or make/unmake recursion based on `CoreSearchConfig.enable_make_unmake`.
- `battlesnake/c-core/core/search_stats.h`: keep existing counters; no new field is required because `clone_calls` and `board_allocations` already prove clone removal.
- `setup.py`: compile `search_state.c`.
- `tests/test_search_diagnostics.py`: clone-vs-make/unmake equivalence and clone-counter regression tests.
- `benchmarks/results/README.md`: document local benchmark artifacts and comparison commands.
- `docs/superpowers/plans/2026-07-03-makeunmake-final-benchmark.md`: track this implementation.

---

### Task 8: In-Place Make/Unmake Search State

**Files:**
- Create: `battlesnake/c-core/core/search_state.h`
- Create: `battlesnake/c-core/core/search_state.c`
- Modify: `setup.py`
- Modify: `battlesnake/c-core/core/core_algorithms.c`
- Test: `tests/test_search_diagnostics.py`

- [ ] **Step 1: Add the clone-vs-make/unmake equivalence test**

Append this test method to `SearchDiagnosticsTests` in `tests/test_search_diagnostics.py`:

```python
    def test_make_unmake_matches_clone_search_result(self) -> None:
        for name in ("duel_open_7x7", "duel_tail_chase_trap", "royale_hazard_ring_duel"):
            with self.subTest(scenario=name):
                scenario = get_scenario(name)
                clone_result = minimax_diagnostics(
                    build_board(scenario),
                    scenario.snake_id,
                    time_budget_ms=1000,
                    fixed_depth=4,
                    enable_make_unmake=False,
                )
                in_place_result = minimax_diagnostics(
                    build_board(scenario),
                    scenario.snake_id,
                    time_budget_ms=1000,
                    fixed_depth=4,
                    enable_make_unmake=True,
                )

                self.assertEqual(in_place_result["move"], clone_result["move"])
                self.assertAlmostEqual(float(in_place_result["score"]), float(clone_result["score"]), places=6)
                self.assertLessEqual(in_place_result["clone_calls"], clone_result["clone_calls"])
                self.assertLessEqual(in_place_result["board_allocations"], clone_result["board_allocations"])
```

- [ ] **Step 2: Run the new test and verify the current behavior**

Run:

```bash
python3 -B -m unittest tests.test_search_diagnostics.SearchDiagnosticsTests.test_make_unmake_matches_clone_search_result -v
```

Expected: PASS before implementation because `enable_make_unmake` is currently an API flag with no search-state effect. This confirms the test is an equivalence guard, not yet a performance guard.

- [ ] **Step 3: Add the make/unmake clone-counter regression test**

Append this test method to `SearchDiagnosticsTests` in `tests/test_search_diagnostics.py`:

```python
    def test_make_unmake_reduces_clone_counters(self) -> None:
        scenario = get_scenario("duel_open_7x7")
        clone_result = minimax_diagnostics(
            build_board(scenario),
            scenario.snake_id,
            time_budget_ms=1000,
            fixed_depth=4,
            enable_make_unmake=False,
        )
        in_place_result = minimax_diagnostics(
            build_board(scenario),
            scenario.snake_id,
            time_budget_ms=1000,
            fixed_depth=4,
            enable_make_unmake=True,
        )

        self.assertEqual(in_place_result["move"], clone_result["move"])
        self.assertEqual(in_place_result["completed_depth"], clone_result["completed_depth"])
        self.assertLess(in_place_result["clone_calls"], clone_result["clone_calls"])
        self.assertLess(in_place_result["board_allocations"], clone_result["board_allocations"])
```

- [ ] **Step 4: Run the clone-counter test and verify it fails**

Run:

```bash
python3 -B -m unittest tests.test_search_diagnostics.SearchDiagnosticsTests.test_make_unmake_reduces_clone_counters -v
```

Expected: FAIL with `AssertionError` because both paths still call `BoardCloneAndApply()` the same number of times.

- [ ] **Step 5: Create the search-state header**

Create `battlesnake/c-core/core/search_state.h`:

```c
#pragma once

#include <stdbool.h>

#include "../datatypes/board.h"

typedef struct {
    char* id;
    char* name;
    int health;
    Coord* body;
    int body_len;
    int length;
} CoreUndoSnake;

typedef struct {
    CoreUndoSnake* snakes;
    int snake_count;
    Coord* food;
    int food_count;
} CoreUndoBoardFrame;

typedef struct {
    Board board;
    CoreUndoBoardFrame* undo_stack;
    int undo_count;
    int undo_capacity;
    Coord* new_heads;
    bool* dead;
    bool* moved_flags;
    int scratch_capacity;
} CoreSearchState;

bool CoreSearchStateInit(CoreSearchState* state, const Board* board);
void CoreSearchStateFree(CoreSearchState* state);
bool CoreSearchStateMakeMoves(CoreSearchState* state, const char** snake_ids, const MoveDirection* moves, int move_count);
bool CoreSearchStateUnmake(CoreSearchState* state);
const Board* CoreSearchStateBoard(const CoreSearchState* state);
```

- [ ] **Step 6: Create search-state helpers and lifecycle**

Create `battlesnake/c-core/core/search_state.c` with this initial content:

```c
#include "search_state.h"

#include <stdlib.h>
#include <string.h>

static char* duplicate_string(const char* value) {
    if (value == NULL) {
        value = "";
    }
    size_t length = strlen(value) + 1;
    char* copy = (char*)malloc(length);
    if (copy != NULL) {
        memcpy(copy, value, length);
    }
    return copy;
}

static bool is_constrictor(const Board* board) {
    return board != NULL && board->ruleset_name != NULL && strcmp(board->ruleset_name, "constrictor") == 0;
}

static int coord_index_in_array(const Coord* coords, int count, Coord target) {
    for (int i = 0; i < count; i++) {
        if (CoordEquals(coords[i], target)) {
            return i;
        }
    }
    return -1;
}

static MoveDirection move_for_snake(const char* snake_id, const char** snake_ids, const MoveDirection* moves, int move_count) {
    for (int i = 0; i < move_count; i++) {
        if (strcmp(snake_id, snake_ids[i]) == 0) {
            return moves[i];
        }
    }
    return MOVE_INVALID;
}

static int snake_length(const Snake* snake) {
    return snake->length > 0 ? snake->length : snake->body_len;
}

static void undo_snake_free(CoreUndoSnake* snake) {
    if (snake == NULL) {
        return;
    }
    free(snake->id);
    free(snake->name);
    free(snake->body);
    memset(snake, 0, sizeof(*snake));
}

static bool undo_snake_copy(CoreUndoSnake* target, const Snake* source) {
    memset(target, 0, sizeof(*target));
    target->id = duplicate_string(source->id);
    target->name = duplicate_string(source->name);
    target->health = source->health;
    target->body_len = source->body_len;
    target->length = source->length;
    if (source->body_len > 0) {
        target->body = (Coord*)malloc((size_t)source->body_len * sizeof(Coord));
        if (target->body == NULL) {
            undo_snake_free(target);
            return false;
        }
        memcpy(target->body, source->body, (size_t)source->body_len * sizeof(Coord));
    }
    if (target->id == NULL || target->name == NULL) {
        undo_snake_free(target);
        return false;
    }
    return true;
}

static void undo_frame_free(CoreUndoBoardFrame* frame) {
    if (frame == NULL) {
        return;
    }
    for (int i = 0; i < frame->snake_count; i++) {
        undo_snake_free(&frame->snakes[i]);
    }
    free(frame->snakes);
    free(frame->food);
    memset(frame, 0, sizeof(*frame));
}

static bool undo_frame_capture(CoreUndoBoardFrame* frame, const Board* board) {
    memset(frame, 0, sizeof(*frame));
    frame->snake_count = board->snake_count;
    frame->food_count = board->food_count;
    if (board->snake_count > 0) {
        frame->snakes = (CoreUndoSnake*)calloc((size_t)board->snake_count, sizeof(CoreUndoSnake));
        if (frame->snakes == NULL) {
            return false;
        }
        for (int i = 0; i < board->snake_count; i++) {
            if (!undo_snake_copy(&frame->snakes[i], &board->snakes[i])) {
                undo_frame_free(frame);
                return false;
            }
        }
    }
    if (board->food_count > 0) {
        frame->food = (Coord*)malloc((size_t)board->food_count * sizeof(Coord));
        if (frame->food == NULL) {
            undo_frame_free(frame);
            return false;
        }
        memcpy(frame->food, board->food, (size_t)board->food_count * sizeof(Coord));
    }
    return true;
}

static bool ensure_undo_capacity(CoreSearchState* state) {
    if (state->undo_count < state->undo_capacity) {
        return true;
    }
    int next_capacity = state->undo_capacity == 0 ? 32 : state->undo_capacity * 2;
    CoreUndoBoardFrame* next = (CoreUndoBoardFrame*)realloc(
        state->undo_stack,
        (size_t)next_capacity * sizeof(CoreUndoBoardFrame)
    );
    if (next == NULL) {
        return false;
    }
    state->undo_stack = next;
    state->undo_capacity = next_capacity;
    return true;
}

static bool ensure_scratch_capacity(CoreSearchState* state, int snake_count) {
    if (snake_count <= state->scratch_capacity) {
        return true;
    }
    Coord* new_heads = (Coord*)realloc(state->new_heads, (size_t)snake_count * sizeof(Coord));
    bool* dead = (bool*)realloc(state->dead, (size_t)snake_count * sizeof(bool));
    bool* moved_flags = (bool*)realloc(state->moved_flags, (size_t)snake_count * sizeof(bool));
    if (new_heads == NULL || dead == NULL || moved_flags == NULL) {
        free(new_heads);
        free(dead);
        free(moved_flags);
        state->new_heads = NULL;
        state->dead = NULL;
        state->moved_flags = NULL;
        state->scratch_capacity = 0;
        return false;
    }
    state->new_heads = new_heads;
    state->dead = dead;
    state->moved_flags = moved_flags;
    state->scratch_capacity = snake_count;
    return true;
}

static void clear_snake(Snake* snake) {
    SnakeFree(snake);
}

static bool copy_board_owned(Board* target, const Board* source) {
    Board* copy = BoardCopy(source);
    if (copy == NULL) {
        return false;
    }
    *target = *copy;
    free(copy);
    return true;
}

bool CoreSearchStateInit(CoreSearchState* state, const Board* board) {
    if (state == NULL || board == NULL) {
        return false;
    }
    memset(state, 0, sizeof(*state));
    return copy_board_owned(&state->board, board);
}

void CoreSearchStateFree(CoreSearchState* state) {
    if (state == NULL) {
        return;
    }
    for (int i = 0; i < state->board.snake_count; i++) {
        clear_snake(&state->board.snakes[i]);
    }
    free(state->board.snakes);
    free(state->board.food);
    free(state->board.hazards);
    free(state->board.ruleset_name);
    for (int i = 0; i < state->undo_count; i++) {
        undo_frame_free(&state->undo_stack[i]);
    }
    free(state->undo_stack);
    free(state->new_heads);
    free(state->dead);
    free(state->moved_flags);
    memset(state, 0, sizeof(*state));
}

const Board* CoreSearchStateBoard(const CoreSearchState* state) {
    return state == NULL ? NULL : &state->board;
}
```

- [ ] **Step 7: Add direct mutation and undo implementation**

Append these functions to `battlesnake/c-core/core/search_state.c`:

```c
static bool ensure_snake_body_capacity(Snake* snake, int body_len) {
    if (body_len <= snake->body_len) {
        return true;
    }
    Coord* next = (Coord*)realloc(snake->body, (size_t)body_len * sizeof(Coord));
    if (next == NULL) {
        return false;
    }
    snake->body = next;
    return true;
}

static bool apply_food_removal(Board* board, const bool* eaten_food) {
    int write = 0;
    for (int i = 0; i < board->food_count; i++) {
        if (!eaten_food[i]) {
            board->food[write++] = board->food[i];
        }
    }
    board->food_count = write;
    return true;
}

static bool move_live_snake(Board* board, int index, MoveDirection move, bool* eaten_food) {
    Snake* snake = &board->snakes[index];
    Coord new_head = MoveStep(SnakeHead(snake), move);
    bool ate_food = false;
    int food_index = coord_index_in_array(board->food, board->food_count, new_head);
    if (food_index >= 0) {
        ate_food = true;
        eaten_food[food_index] = true;
    }
    bool grew = ate_food || is_constrictor(board);
    int new_body_len = snake->body_len + (grew ? 1 : 0);
    if (!ensure_snake_body_capacity(snake, new_body_len)) {
        return false;
    }
    int copy_count = grew ? snake->body_len : snake->body_len - 1;
    if (copy_count > 0) {
        memmove(&snake->body[1], snake->body, (size_t)copy_count * sizeof(Coord));
    }
    snake->body[0] = new_head;
    snake->body_len = new_body_len;
    snake->length = new_body_len;
    snake->health -= 1;
    if (coord_index_in_array(board->hazards, board->hazard_count, new_head) >= 0) {
        snake->health -= board->hazard_damage;
    }
    if (ate_food) {
        snake->health = 100;
    }
    return true;
}

static void resolve_body_collisions(Board* board, const Coord* new_heads, bool* dead) {
    for (int i = 0; i < board->snake_count; i++) {
        if (dead[i]) {
            continue;
        }
        Coord head = new_heads[i];
        for (int j = 0; j < board->snake_count; j++) {
            const Snake* other = &board->snakes[j];
            for (int k = 1; k < other->body_len; k++) {
                if (CoordEquals(head, other->body[k])) {
                    dead[i] = true;
                }
            }
        }
    }
}

static void resolve_head_collisions(Board* board, const Coord* new_heads, bool* dead) {
    for (int i = 0; i < board->snake_count; i++) {
        if (dead[i]) {
            continue;
        }
        for (int j = i + 1; j < board->snake_count; j++) {
            if (dead[j] || !CoordEquals(new_heads[i], new_heads[j])) {
                continue;
            }
            int left_len = snake_length(&board->snakes[i]);
            int right_len = snake_length(&board->snakes[j]);
            if (left_len > right_len) {
                dead[j] = true;
            } else if (right_len > left_len) {
                dead[i] = true;
            } else {
                dead[i] = true;
                dead[j] = true;
            }
        }
    }
}

static void compact_live_snakes(Board* board, const bool* dead) {
    int write = 0;
    for (int read = 0; read < board->snake_count; read++) {
        if (dead[read]) {
            clear_snake(&board->snakes[read]);
            continue;
        }
        if (write != read) {
            board->snakes[write] = board->snakes[read];
            memset(&board->snakes[read], 0, sizeof(Snake));
        }
        write++;
    }
    board->snake_count = write;
}

bool CoreSearchStateMakeMoves(CoreSearchState* state, const char** snake_ids, const MoveDirection* moves, int move_count) {
    if (state == NULL || snake_ids == NULL || moves == NULL || move_count < 0) {
        return false;
    }
    Board* board = &state->board;
    if (!ensure_undo_capacity(state) || !ensure_scratch_capacity(state, board->snake_count)) {
        return false;
    }
    CoreUndoBoardFrame* frame = &state->undo_stack[state->undo_count];
    if (!undo_frame_capture(frame, board)) {
        return false;
    }

    memset(state->dead, 0, (size_t)board->snake_count * sizeof(bool));
    memset(state->moved_flags, 0, (size_t)board->snake_count * sizeof(bool));
    bool* eaten_food = NULL;
    if (board->food_count > 0) {
        eaten_food = (bool*)calloc((size_t)board->food_count, sizeof(bool));
        if (eaten_food == NULL) {
            undo_frame_free(frame);
            return false;
        }
    }

    for (int i = 0; i < board->snake_count; i++) {
        Snake* snake = &board->snakes[i];
        MoveDirection move = move_for_snake(snake->id, snake_ids, moves, move_count);
        if (move == MOVE_INVALID || snake->body_len == 0) {
            state->dead[i] = true;
            state->new_heads[i] = SnakeHead(snake);
            continue;
        }
        state->new_heads[i] = MoveStep(SnakeHead(snake), move);
        state->moved_flags[i] = true;
        if (!move_live_snake(board, i, move, eaten_food)) {
            free(eaten_food);
            undo_frame_free(frame);
            return false;
        }
        if (!BoardInBounds(board, state->new_heads[i]) || board->snakes[i].health <= 0) {
            state->dead[i] = true;
        }
    }

    resolve_body_collisions(board, state->new_heads, state->dead);
    resolve_head_collisions(board, state->new_heads, state->dead);
    compact_live_snakes(board, state->dead);
    if (eaten_food != NULL) {
        apply_food_removal(board, eaten_food);
    }
    free(eaten_food);
    state->undo_count++;
    return true;
}

bool CoreSearchStateUnmake(CoreSearchState* state) {
    if (state == NULL || state->undo_count <= 0) {
        return false;
    }
    Board* board = &state->board;
    for (int i = 0; i < board->snake_count; i++) {
        clear_snake(&board->snakes[i]);
    }
    CoreUndoBoardFrame* frame = &state->undo_stack[state->undo_count - 1];
    if (frame->snake_count > 0) {
        Snake* restored = (Snake*)realloc(board->snakes, (size_t)frame->snake_count * sizeof(Snake));
        if (restored == NULL) {
            return false;
        }
        board->snakes = restored;
    }
    for (int i = 0; i < frame->snake_count; i++) {
        board->snakes[i].id = frame->snakes[i].id;
        board->snakes[i].name = frame->snakes[i].name;
        board->snakes[i].health = frame->snakes[i].health;
        board->snakes[i].body = frame->snakes[i].body;
        board->snakes[i].body_len = frame->snakes[i].body_len;
        board->snakes[i].length = frame->snakes[i].length;
        memset(&frame->snakes[i], 0, sizeof(CoreUndoSnake));
    }
    board->snake_count = frame->snake_count;

    if (frame->food_count > 0) {
        Coord* restored_food = (Coord*)realloc(board->food, (size_t)frame->food_count * sizeof(Coord));
        if (restored_food == NULL) {
            return false;
        }
        board->food = restored_food;
        memcpy(board->food, frame->food, (size_t)frame->food_count * sizeof(Coord));
    }
    board->food_count = frame->food_count;
    undo_frame_free(frame);
    state->undo_count--;
    return true;
}
```

- [ ] **Step 8: Compile the search-state source**

Add the source file to `SOURCE_FILES` in `setup.py` immediately after `search_workspace.c`:

```python
    "battlesnake/c-core/core/search_state.c",
```

Run:

```bash
python3 setup.py build_ext --inplace --force
```

Expected: build succeeds. If it fails with a compiler diagnostic, fix the exact line reported before continuing.

- [ ] **Step 9: Route recursive search through make/unmake**

In `battlesnake/c-core/core/core_algorithms.c`, add the include:

```c
#include "search_state.h"
```

Add this field to `CoreSearchContext`:

```c
CoreSearchState* state;
```

In `CoreMinimaxMoveWithStats()`, after `CoreSearchWorkspaceInit(...)`, initialize state only when enabled:

```c
CoreSearchState state;
memset(&state, 0, sizeof(state));
if (config.enable_make_unmake) {
    if (!CoreSearchStateInit(&state, board)) {
        CoreSearchWorkspaceFree(&context.workspace);
        CoreTtFree(&context.tt);
        return CORE_ERROR;
    }
    context.state = &state;
}
```

Before every return from `CoreMinimaxMoveWithStats()` after this initialization, free state when enabled:

```c
if (context.state != NULL) {
    CoreSearchStateFree(context.state);
}
```

Replace the `BoardCloneAndApply()` block in `core_minimax_search()` with:

```c
            if (context->config.enable_make_unmake && context->state != NULL) {
                if (!CoreSearchStateMakeMoves(context->state, ids, moves, snake_count)) {
                    return CORE_ERROR;
                }
                const Board* next = CoreSearchStateBoard(context->state);
                CoreStatus status = core_minimax_search(
                    next,
                    snake_id,
                    depth - 1,
                    ply + 1,
                    alpha,
                    child_beta,
                    MOVE_INVALID,
                    context,
                    timed_out,
                    &score,
                    NULL
                );
                if (!CoreSearchStateUnmake(context->state)) {
                    return CORE_ERROR;
                }
                if (status != CORE_OK || *timed_out) {
                    return status;
                }
            } else {
                if (stats != NULL) {
                    stats->clone_calls++;
                    stats->board_allocations++;
                }
                Board* next = BoardCloneAndApply(board, ids, moves, snake_count);
                if (next == NULL) {
                    return CORE_ERROR;
                }
                CoreStatus status = core_minimax_search(
                    next,
                    snake_id,
                    depth - 1,
                    ply + 1,
                    alpha,
                    child_beta,
                    MOVE_INVALID,
                    context,
                    timed_out,
                    &score,
                    NULL
                );
                BoardFree(next);
                if (status != CORE_OK || *timed_out) {
                    return status;
                }
            }
```

- [ ] **Step 10: Run Task 8 tests**

Run:

```bash
python3 -B -m unittest tests.test_search_diagnostics.SearchDiagnosticsTests.test_make_unmake_matches_clone_search_result tests.test_search_diagnostics.SearchDiagnosticsTests.test_make_unmake_reduces_clone_counters -v
```

Expected: both tests pass. `test_make_unmake_reduces_clone_counters` must show lower `clone_calls` and `board_allocations` on the make/unmake path.

- [ ] **Step 11: Run focused regression suites**

Run:

```bash
python3 -B -m unittest tests.test_search_diagnostics tests.test_benchmark_scenarios tests.test_zobrist_hash -v
```

Expected: all tests pass. Current expected count after adding the two Task 8 tests is 19 tests.

- [ ] **Step 12: Generate the make/unmake benchmark artifact**

Run:

```bash
python3 -B -m benchmarks.run_minimax_bench --runs 20 --warmup 3 --budgets 180,320,450 --fixed-depths 0,4,6 --out benchmarks/results/after-makeunmake.jsonl
```

Expected: command completes and writes 72 JSONL rows.

- [ ] **Step 13: Compare against the workspace baseline**

Run:

```bash
python3 -B -m benchmarks.compare_benchmarks benchmarks/results/after-workspace.jsonl benchmarks/results/after-makeunmake.jsonl
wc -l benchmarks/results/after-makeunmake.jsonl
```

Expected: comparator exits 0; `wc` prints `72 benchmarks/results/after-makeunmake.jsonl`. Fixed-depth rows should show lower `clone_calls_p50` and lower `board_allocations_p50` than `after-workspace.jsonl`.

- [ ] **Step 14: Commit Task 8**

Run:

```bash
git add setup.py battlesnake/c-core/core/core_algorithms.c battlesnake/c-core/core/search_state.h battlesnake/c-core/core/search_state.c tests/test_search_diagnostics.py benchmarks/results/after-makeunmake.jsonl
git commit -m "perf: use minimax make-unmake search state"
```

Expected: commit succeeds with only Task 8 files staged.

---

### Task 9: Final Benchmark Report And Guardrails

**Files:**
- Create: `benchmarks/results/README.md`
- Modify: `docs/superpowers/plans/2026-07-03-makeunmake-final-benchmark.md`

- [ ] **Step 1: Create benchmark result documentation**

Create `benchmarks/results/README.md`:

```markdown
# Benchmark Results

Generated benchmark files in this directory are local performance artifacts. Keep the named JSONL files when working on search performance so comparisons stay reproducible:

- `baseline-before-tt.jsonl`
- `after-tt.jsonl`
- `after-ordering.jsonl`
- `after-workspace.jsonl`
- `after-makeunmake.jsonl`
- `final.jsonl`

Use the comparator from the repository root:

```bash
python3 -B -m benchmarks.compare_benchmarks benchmarks/results/baseline-before-tt.jsonl benchmarks/results/final.jsonl
```

Budget rows are gated on completed depth and elapsed p50. Fixed-depth rows are gated on move stability; fixed-depth elapsed p50 is printed for review because low-millisecond rows are sensitive to host scheduler noise.
```

- [ ] **Step 2: Run full final verification**

Run:

```bash
python3 setup.py build_ext --inplace --force
python3 -B -m unittest discover -s tests -p 'test_*.py' -v
python3 -B -m benchmarks.run_minimax_bench --runs 30 --warmup 5 --budgets 180,320,450 --fixed-depths 0,4,6,8 --out benchmarks/results/final.jsonl
python3 -B -m benchmarks.compare_benchmarks benchmarks/results/baseline-before-tt.jsonl benchmarks/results/final.jsonl
```

Expected: build succeeds; unit tests pass; final benchmark writes 96 JSONL rows; comparator exits 0.

- [ ] **Step 3: Extract final summary numbers**

Run:

```bash
python3 -B -c 'import json
from pathlib import Path
base=[json.loads(l) for l in Path("benchmarks/results/baseline-before-tt.jsonl").read_text().splitlines()]
final=[json.loads(l) for l in Path("benchmarks/results/final.jsonl").read_text().splitlines()]
by_base={(r["scenario"], r["budget_ms"], r["fixed_depth"]): r for r in base}
depth_gains=[]
nps_gains=[]
for row in final:
    key=(row["scenario"], row["budget_ms"], row["fixed_depth"])
    if key not in by_base:
        continue
    before=by_base[key]
    depth_gains.append((float(row["completed_depth"])-float(before["completed_depth"]), key, before["completed_depth"], row["completed_depth"]))
    before_nps=max(float(before["nodes_per_second_p50"]), 1.0)
    nps_gains.append((float(row["nodes_per_second_p50"])/before_nps, key, before["nodes_per_second_p50"], row["nodes_per_second_p50"]))
print("biggest_depth_gain", max(depth_gains))
print("biggest_nps_gain", max(nps_gains))
print("weakest_nps_gain", min(nps_gains))
print("final_rows", len(final))'
```

Expected: command prints `final_rows 96` plus biggest/weakest gain tuples.

- [ ] **Step 4: Append execution summary to this plan**

Run this command to append the actual summary values to `docs/superpowers/plans/2026-07-03-makeunmake-final-benchmark.md`:

```bash
python3 -B -c 'import json
from pathlib import Path
plan = Path("docs/superpowers/plans/2026-07-03-makeunmake-final-benchmark.md")
base = [json.loads(l) for l in Path("benchmarks/results/baseline-before-tt.jsonl").read_text().splitlines()]
final = [json.loads(l) for l in Path("benchmarks/results/final.jsonl").read_text().splitlines()]
by_base = {(r["scenario"], r["budget_ms"], r["fixed_depth"]): r for r in base}
depth_gains = []
nps_gains = []
for row in final:
    key = (row["scenario"], row["budget_ms"], row["fixed_depth"])
    if key not in by_base:
        continue
    before = by_base[key]
    depth_gains.append((float(row["completed_depth"]) - float(before["completed_depth"]), key, before["completed_depth"], row["completed_depth"]))
    before_nps = max(float(before["nodes_per_second_p50"]), 1.0)
    nps_gains.append((float(row["nodes_per_second_p50"]) / before_nps, key, before["nodes_per_second_p50"], row["nodes_per_second_p50"]))
summary = "\n## Execution Summary\n\n"
summary += "- Baseline file: `benchmarks/results/baseline-before-tt.jsonl`\n"
summary += "- Final file: `benchmarks/results/final.jsonl`\n"
summary += f"- Final row count: {len(final)}\n"
summary += f"- Biggest depth gain: {max(depth_gains)}\n"
summary += f"- Biggest nodes/sec gain: {max(nps_gains)}\n"
summary += f"- Weakest nodes/sec gain: {min(nps_gains)}\n"
summary += "- Residual risk: Fixed-depth elapsed p50 is useful for review but noisy on this host; budget-depth gates and move stability are the hard pass/fail checks.\n"
plan.write_text(plan.read_text() + summary)'
```

Expected: the plan ends with an `Execution Summary` section containing concrete tuple values and `Final row count: 96`.

- [ ] **Step 5: Commit Task 9**

Run:

```bash
git add benchmarks/results/README.md benchmarks/results/final.jsonl docs/superpowers/plans/2026-07-03-makeunmake-final-benchmark.md
git commit -m "docs: record final minimax benchmark workflow"
```

Expected: commit succeeds with only Task 9 files staged.

---

## Self-Review

Spec coverage:

- Task 8 covers the remaining make/unmake implementation, equivalence tests, reduced clone counters, benchmark generation, and comparison against Task 7.
- Task 9 covers benchmark documentation, final 30-run verification, final summary extraction, and final benchmark artifact.
- The plan starts from the current Task 7 state and does not repeat completed Tasks 1-7.

Placeholder scan:

- No placeholder markers are present.
- Every code-changing step includes concrete code or a concrete replacement snippet.
- Every verification step includes exact commands and expected results.

Type consistency:

- `CoreSearchState`, `CoreUndoBoardFrame`, and `CoreUndoSnake` are defined before use.
- `CoreSearchStateMakeMoves()`, `CoreSearchStateUnmake()`, and `CoreSearchStateBoard()` signatures match between header, C implementation, and `core_algorithms.c` usage.
- Python tests use the existing `minimax_diagnostics()` keyword flags already exposed by the native wrapper.
