# Search Performance Optimization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a measured, repeatable optimization pipeline for Battlesnake minimax, then implement transposition tables with Zobrist hashing, deeper move ordering, and allocation-churn reduction through in-place search simulation.

**Architecture:** Add a first-class C search diagnostics API that reports depth, node, cutoff, allocation, clone, and transposition-table metrics without changing the production `minimax_move()` API. Drive benchmarks from Python over a suite of deterministic realistic board scenarios, and require every optimization task to publish before/after benchmark output. Keep search optimizations in focused C modules so `core_algorithms.c` does not become a single unreviewable blob.

**Tech Stack:** CPython extension in C (`setup.py build_ext --inplace --force`), native Battlesnake `Board`/`Snake`/`Coord` structs, Python `unittest`, deterministic benchmark scripts under `benchmarks/`, JSONL benchmark output.

---

## Scope And Order

This is one performance project with four deliverable phases:

1. Instrumentation and realistic benchmarks.
2. Zobrist hashing plus transposition table.
3. Move ordering using iterative-deepening best moves, TT best moves, killer moves, and history scores.
4. Allocation-churn reduction, ending with in-place make/unmake inside search.

Do not start phase 2 until phase 1 produces baseline JSONL numbers. Do not start phase 3 until TT hit-rate is visible in benchmark output. Do not start phase 4 until clone/allocation counters identify the remaining hot paths.

## File Structure

Create:

- `benchmarks/__init__.py`: makes benchmark helpers importable from tests and scripts.
- `benchmarks/scenarios.py`: deterministic realistic board fixtures. No random boards in baseline comparisons.
- `benchmarks/run_minimax_bench.py`: benchmark runner that prints and writes JSONL rows.
- `benchmarks/compare_benchmarks.py`: compares two JSONL benchmark outputs and flags regressions.
- `benchmarks/results/.gitkeep`: keeps the output directory present; actual benchmark JSONL files stay untracked.
- `tests/__init__.py`: enables `unittest discover`.
- `tests/test_benchmark_scenarios.py`: verifies every fixture builds, has a living controlled snake, and has legal moves.
- `tests/test_search_diagnostics.py`: verifies the Python diagnostics API shape and sane counters.
- `battlesnake/c-core/core/search_stats.h`: public C diagnostics structs and reset helpers.
- `battlesnake/c-core/core/search_stats.c`: helpers for stats initialization and aggregation.
- `battlesnake/c-core/core/zobrist.h`: Zobrist hash API.
- `battlesnake/c-core/core/zobrist.c`: deterministic 64-bit hashing for board state.
- `battlesnake/c-core/core/transposition_table.h`: fixed-size TT interface.
- `battlesnake/c-core/core/transposition_table.c`: TT allocation, probe, store, generation reset.
- `battlesnake/c-core/core/search_workspace.h`: reusable buffers for search.
- `battlesnake/c-core/core/search_workspace.c`: buffer allocation, occupancy bitmap, move arrays.
- `battlesnake/c-core/core/search_state.h`: mutable search state and undo records.
- `battlesnake/c-core/core/search_state.c`: make/unmake implementation for simultaneous moves.

Modify:

- `setup.py`: include new C source files.
- `battlesnake/c-core/core/core_algorithms.h`: expose `CoreMinimaxMoveWithStats()`.
- `battlesnake/c-core/core/core_algorithms.c`: route minimax through search context, stats, TT, ordering, and later make/unmake.
- `battlesnake/c-core/py-core/py_core.c`: expose `minimax_diagnostics()`.
- `battlesnake/battlesnake_native.pyi`: add `minimax_diagnostics()` type signature.
- `.gitignore`: ignore benchmark result JSONL files.

## Benchmark Scenarios

The benchmark suite must cover these deterministic cases:

- `duel_open_7x7`: common early duel with space and a center food race.
- `duel_center_pressure_11x11`: both snakes contest center with multiple branch choices.
- `duel_low_health_food_race`: low-health snake where pathfinding/evaluation matters.
- `duel_tail_chase_trap`: tail-vacating and body collision logic matter.
- `duel_corridor_choke`: narrow corridor with high alpha-beta pruning potential.
- `duel_late_game_long_bodies`: long bodies and dense board pressure.
- `royale_hazard_ring_duel`: hazard penalties and hazard survival.
- `standard_four_snakes_dense`: multi-snake branching, not only duel.

Each benchmark run must report at least:

- `scenario`, `budget_ms`, `fixed_depth`, `runs`, `warmup`
- `move`, `completed_depth`, `max_depth_started`, `timed_out`
- `elapsed_ms_min`, `elapsed_ms_p50`, `elapsed_ms_p95`, `elapsed_ms_max`
- `nodes_min`, `nodes_p50`, `nodes_max`, `nodes_per_second_p50`
- `leaf_evals_p50`, `clone_calls_p50`, `board_allocations_p50`
- `beta_cutoffs_p50`, `move_order_first_choice_cutoffs_p50`
- `tt_probes_p50`, `tt_hits_p50`, `tt_hit_rate_p50`, `tt_cutoffs_p50`, `tt_stores_p50`

The benchmark runner must support both production budget mode and fixed-depth mode:

- Budget mode answers: “How deep do we get in 180/320/450 ms?”
- Fixed-depth mode answers: “How many nodes/sec and allocations/node do we spend at depth N?”

---

### Task 1: Benchmark Fixtures And Failing Scenario Tests

**Files:**
- Create: `benchmarks/__init__.py`
- Create: `benchmarks/scenarios.py`
- Create: `tests/__init__.py`
- Create: `tests/test_benchmark_scenarios.py`
- Modify: `.gitignore`

- [ ] **Step 1: Create benchmark package marker**

Create `benchmarks/__init__.py`:

```python
"""Benchmark helpers for native Battlesnake search."""
```

- [ ] **Step 2: Create deterministic realistic scenarios**

Create `benchmarks/scenarios.py`:

```python
"""Deterministic board fixtures for minimax benchmarks."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Iterable

from battlesnake.battlesnake_native import Board, Coord, Snake


@dataclass(frozen=True)
class Scenario:
    name: str
    width: int
    height: int
    ruleset_name: str
    hazard_damage: int
    snakes: tuple[Snake, ...]
    food: tuple[Coord, ...]
    hazards: tuple[Coord, ...]
    snake_id: str = "me"


def _snake(snake_id: str, body: Iterable[tuple[int, int]], health: int = 90) -> Snake:
    coords = [Coord(x, y) for x, y in body]
    return Snake(id=snake_id, name=snake_id, health=health, body=coords, length=len(coords))


def _coords(values: Iterable[tuple[int, int]]) -> tuple[Coord, ...]:
    return tuple(Coord(x, y) for x, y in values)


SCENARIOS: tuple[Scenario, ...] = (
    Scenario(
        name="duel_open_7x7",
        width=7,
        height=7,
        ruleset_name="standard",
        hazard_damage=0,
        snakes=(
            _snake("me", [(1, 3), (1, 2), (1, 1)], 90),
            _snake("you", [(5, 3), (5, 2), (5, 1)], 90),
        ),
        food=_coords([(3, 3), (2, 5), (4, 5)]),
        hazards=(),
    ),
    Scenario(
        name="duel_center_pressure_11x11",
        width=11,
        height=11,
        ruleset_name="standard",
        hazard_damage=0,
        snakes=(
            _snake("me", [(4, 5), (4, 4), (3, 4), (2, 4), (2, 3)], 83),
            _snake("you", [(6, 5), (6, 6), (7, 6), (8, 6), (8, 7)], 88),
        ),
        food=_coords([(5, 5), (1, 8), (9, 2), (5, 8)]),
        hazards=(),
    ),
    Scenario(
        name="duel_low_health_food_race",
        width=11,
        height=11,
        ruleset_name="standard",
        hazard_damage=0,
        snakes=(
            _snake("me", [(3, 5), (3, 4), (3, 3), (2, 3)], 14),
            _snake("you", [(7, 5), (7, 6), (7, 7), (8, 7)], 72),
        ),
        food=_coords([(5, 5), (2, 8), (8, 2)]),
        hazards=(),
    ),
    Scenario(
        name="duel_tail_chase_trap",
        width=11,
        height=11,
        ruleset_name="standard",
        hazard_damage=0,
        snakes=(
            _snake("me", [(5, 2), (5, 1), (4, 1), (3, 1), (3, 2), (3, 3)], 77),
            _snake("you", [(5, 8), (5, 9), (6, 9), (7, 9), (7, 8), (7, 7)], 80),
        ),
        food=_coords([(5, 5), (1, 1), (9, 9)]),
        hazards=(),
    ),
    Scenario(
        name="duel_corridor_choke",
        width=11,
        height=11,
        ruleset_name="standard",
        hazard_damage=0,
        snakes=(
            _snake("me", [(2, 5), (2, 4), (2, 3), (1, 3), (1, 2), (1, 1)], 90),
            _snake("you", [(8, 5), (8, 6), (8, 7), (9, 7), (9, 8), (9, 9)], 90),
        ),
        food=_coords([(5, 5), (5, 4)]),
        hazards=_coords([(4, 0), (4, 1), (4, 2), (4, 3), (4, 7), (4, 8), (4, 9), (4, 10), (6, 0), (6, 1), (6, 2), (6, 3), (6, 7), (6, 8), (6, 9), (6, 10)]),
    ),
    Scenario(
        name="duel_late_game_long_bodies",
        width=11,
        height=11,
        ruleset_name="standard",
        hazard_damage=0,
        snakes=(
            _snake("me", [(3, 7), (3, 6), (2, 6), (1, 6), (1, 5), (1, 4), (2, 4), (3, 4), (4, 4), (4, 3), (4, 2), (3, 2)], 64),
            _snake("you", [(7, 3), (7, 4), (8, 4), (9, 4), (9, 5), (9, 6), (8, 6), (7, 6), (6, 6), (6, 7), (6, 8), (7, 8)], 68),
        ),
        food=_coords([(5, 5), (2, 9), (8, 1)]),
        hazards=(),
    ),
    Scenario(
        name="royale_hazard_ring_duel",
        width=11,
        height=11,
        ruleset_name="royale",
        hazard_damage=15,
        snakes=(
            _snake("me", [(4, 5), (4, 4), (4, 3), (3, 3)], 55),
            _snake("you", [(6, 5), (6, 6), (6, 7), (7, 7)], 60),
        ),
        food=_coords([(5, 5), (3, 5), (7, 5)]),
        hazards=_coords([(x, 0) for x in range(11)] + [(x, 10) for x in range(11)] + [(0, y) for y in range(1, 10)] + [(10, y) for y in range(1, 10)]),
    ),
    Scenario(
        name="standard_four_snakes_dense",
        width=11,
        height=11,
        ruleset_name="standard",
        hazard_damage=0,
        snakes=(
            _snake("me", [(2, 2), (2, 1), (1, 1), (1, 0)], 82),
            _snake("north", [(5, 8), (5, 9), (4, 9), (3, 9)], 90),
            _snake("east", [(8, 5), (9, 5), (9, 4), (9, 3)], 90),
            _snake("west", [(2, 7), (1, 7), (1, 8), (1, 9)], 90),
        ),
        food=_coords([(5, 5), (3, 3), (7, 7), (5, 2)]),
        hazards=(),
    ),
)


def scenario_names() -> list[str]:
    return [scenario.name for scenario in SCENARIOS]


def get_scenario(name: str) -> Scenario:
    for scenario in SCENARIOS:
        if scenario.name == name:
            return scenario
    raise KeyError(f"unknown scenario: {name}")


def build_board(scenario: Scenario) -> Board:
    return Board(
        width=scenario.width,
        height=scenario.height,
        snakes={snake.id: snake for snake in scenario.snakes},
        food=scenario.food,
        hazards=scenario.hazards,
        ruleset_name=scenario.ruleset_name,
        hazard_damage=scenario.hazard_damage,
    )
```

- [ ] **Step 3: Add scenario tests**

Create `tests/__init__.py`:

```python
"""Tests for the Battlesnake bot."""
```

Create `tests/test_benchmark_scenarios.py`:

```python
from __future__ import annotations

import unittest

from benchmarks.scenarios import SCENARIOS, build_board


class BenchmarkScenarioTests(unittest.TestCase):
    def test_every_scenario_builds_with_living_controlled_snake(self) -> None:
        self.assertGreaterEqual(len(SCENARIOS), 8)
        for scenario in SCENARIOS:
            with self.subTest(scenario=scenario.name):
                board = build_board(scenario)
                self.assertIn(scenario.snake_id, board.snakes)
                self.assertGreater(len(board.snakes[scenario.snake_id].body), 0)
                self.assertIn(board.head(scenario.snake_id), board.occupied(include_tails=True))

    def test_every_scenario_has_at_least_one_safe_move(self) -> None:
        for scenario in SCENARIOS:
            with self.subTest(scenario=scenario.name):
                board = build_board(scenario)
                self.assertGreater(len(board.safe_moves(scenario.snake_id)), 0)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 4: Ignore generated benchmark outputs**

Append to `.gitignore`:

```gitignore

# Benchmark outputs
benchmarks/results/*.jsonl
benchmarks/results/*.json
benchmarks/results/*.csv
```

- [ ] **Step 5: Run tests**

Run:

```bash
python3 -m unittest tests.test_benchmark_scenarios -v
```

Expected:

```text
test_every_scenario_builds_with_living_controlled_snake ... ok
test_every_scenario_has_at_least_one_safe_move ... ok
```

- [ ] **Step 6: Commit**

```bash
git add .gitignore benchmarks/__init__.py benchmarks/scenarios.py tests/__init__.py tests/test_benchmark_scenarios.py
git commit -m "test: add realistic minimax benchmark scenarios"
```

---

### Task 2: C Search Diagnostics API

**Files:**
- Create: `battlesnake/c-core/core/search_stats.h`
- Create: `battlesnake/c-core/core/search_stats.c`
- Create: `tests/test_search_diagnostics.py`
- Modify: `setup.py`
- Modify: `battlesnake/c-core/core/core_algorithms.h`
- Modify: `battlesnake/c-core/core/core_algorithms.c`
- Modify: `battlesnake/c-core/py-core/py_core.c`
- Modify: `battlesnake/battlesnake_native.pyi`

- [ ] **Step 1: Write failing diagnostics test**

Create `tests/test_search_diagnostics.py`:

```python
from __future__ import annotations

import unittest

from battlesnake.battlesnake_native import minimax_diagnostics
from benchmarks.scenarios import get_scenario, build_board


class SearchDiagnosticsTests(unittest.TestCase):
    def test_minimax_diagnostics_shape(self) -> None:
        scenario = get_scenario("duel_open_7x7")
        board = build_board(scenario)
        result = minimax_diagnostics(board, scenario.snake_id, time_budget_ms=25)

        self.assertIn(result["move"], {"up", "down", "left", "right"})
        self.assertIsInstance(result["score"], float)
        self.assertGreaterEqual(result["completed_depth"], 0)
        self.assertGreaterEqual(result["max_depth_started"], result["completed_depth"])
        self.assertGreater(result["nodes"], 0)
        self.assertGreater(result["leaf_evals"], 0)
        self.assertGreaterEqual(result["clone_calls"], 0)
        self.assertGreaterEqual(result["board_allocations"], 0)
        self.assertGreaterEqual(result["beta_cutoffs"], 0)
        self.assertGreaterEqual(result["elapsed_ms"], 0.0)
        self.assertIn(result["timed_out"], {True, False})

    def test_fixed_depth_diagnostics_are_deterministic_enough_for_regression_tests(self) -> None:
        scenario = get_scenario("duel_open_7x7")
        board = build_board(scenario)
        first = minimax_diagnostics(board, scenario.snake_id, time_budget_ms=1000, fixed_depth=3)
        second = minimax_diagnostics(board, scenario.snake_id, time_budget_ms=1000, fixed_depth=3)

        self.assertEqual(first["move"], second["move"])
        self.assertEqual(first["completed_depth"], 3)
        self.assertEqual(second["completed_depth"], 3)
        self.assertEqual(first["nodes"], second["nodes"])


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run test and verify failure**

Run:

```bash
python3 -m unittest tests.test_search_diagnostics -v
```

Expected failure:

```text
ImportError: cannot import name 'minimax_diagnostics'
```

- [ ] **Step 3: Add stats structs**

Create `battlesnake/c-core/core/search_stats.h`:

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "../datatypes/board.h"

typedef struct {
    uint64_t nodes;
    uint64_t leaf_evals;
    uint64_t clone_calls;
    uint64_t board_allocations;
    uint64_t safe_move_calls;
    uint64_t beta_cutoffs;
    uint64_t move_order_first_choice_cutoffs;
    uint64_t tt_probes;
    uint64_t tt_hits;
    uint64_t tt_exact_hits;
    uint64_t tt_lower_hits;
    uint64_t tt_upper_hits;
    uint64_t tt_cutoffs;
    uint64_t tt_stores;
    uint64_t tt_collisions;
    int completed_depth;
    int max_depth_started;
    bool timed_out;
    double score;
    double elapsed_ms;
    MoveDirection move;
} CoreSearchStats;

typedef struct {
    int time_budget_ms;
    int fixed_depth;
    bool enable_tt;
    bool enable_move_ordering;
    bool enable_make_unmake;
} CoreSearchConfig;

CoreSearchConfig CoreSearchConfigDefault(int time_budget_ms);
void CoreSearchStatsInit(CoreSearchStats* stats);
```

Create `battlesnake/c-core/core/search_stats.c`:

```c
#include "search_stats.h"

#include <string.h>

CoreSearchConfig CoreSearchConfigDefault(int time_budget_ms) {
    CoreSearchConfig config;
    config.time_budget_ms = time_budget_ms;
    config.fixed_depth = 0;
    config.enable_tt = true;
    config.enable_move_ordering = true;
    config.enable_make_unmake = true;
    return config;
}

void CoreSearchStatsInit(CoreSearchStats* stats) {
    if (stats == NULL) {
        return;
    }
    memset(stats, 0, sizeof(*stats));
    stats->move = MOVE_INVALID;
}
```

- [ ] **Step 4: Add source file to extension build**

Modify `setup.py` so `SOURCE_FILES` includes `search_stats.c` immediately after `core_algorithms.c`:

```python
SOURCE_FILES = [
    "battlesnake/c-core/datatypes/coord.c",
    "battlesnake/c-core/datatypes/snake.c",
    "battlesnake/c-core/datatypes/board.c",
    "battlesnake/c-core/core/core_algorithms.c",
    "battlesnake/c-core/core/search_stats.c",
    "battlesnake/c-core/py-datatypes/py_datatypes.c",
    "battlesnake/c-core/py-core/py_core.c",
    "battlesnake/c-core/py-datatypes/init_module.c",
]
```

- [ ] **Step 5: Expose C diagnostics entrypoint**

Modify `battlesnake/c-core/core/core_algorithms.h`:

```c
#include "search_stats.h"
```

Add below `CoreMinimaxMove()`:

```c
CoreStatus CoreMinimaxMoveWithStats(
    const Board* board,
    const char* snake_id,
    CoreSearchConfig config,
    MoveDirection* out_move,
    CoreSearchStats* out_stats
);
```

- [ ] **Step 6: Thread stats through existing minimax**

In `battlesnake/c-core/core/core_algorithms.c`, include the new header through `core_algorithms.h`, add elapsed time helper near `core_search_timer_start()`:

```c
static double core_elapsed_ms(struct timespec start, struct timespec end) {
    return (double)(end.tv_sec - start.tv_sec) * 1000.0 +
        (double)(end.tv_nsec - start.tv_nsec) / 1000000.0;
}
```

Thread a `CoreSearchStats* stats` parameter through the recursive search so the
increments below have something to write to. `core_minimax_search()` currently
takes `(board, snake_id, depth, alpha, beta, preferred_move, timer, timed_out,
out_score, out_best_move)` and has no access to stats. Add `CoreSearchStats* stats`
as a parameter (place it right after `timer`), forward it unchanged in the
recursive `core_minimax_search(...)` call inside the combo loop, and pass the
caller's `stats` from `CoreMinimaxMoveWithStats()`. Task 5 replaces this `timer`
+ `stats` parameter pair with a single `CoreSearchContext* context`, so keep the
two as explicit parameters for now.

Increment `nodes` once at the top of `core_minimax_search()`, right after the
timeout check:

```c
if (stats != NULL) {
    stats->nodes++;
}
```

Add before each `CoreEvaluate()` leaf:

```c
if (stats != NULL) {
    stats->leaf_evals++;
}
```

Add before each `core_safe_moves_or_all()` call:

```c
if (stats != NULL) {
    stats->safe_move_calls++;
}
```

Add before `BoardCloneAndApply()`:

```c
if (stats != NULL) {
    stats->clone_calls++;
    stats->board_allocations++;
}
```

Add when `worst_reply <= alpha`:

```c
if (stats != NULL) {
    stats->beta_cutoffs++;
    if (combo == 0) {
        stats->move_order_first_choice_cutoffs++;
    }
}
```

Implement `CoreMinimaxMoveWithStats()` by moving the body of `CoreMinimaxMove()` into the stats-aware function and making `CoreMinimaxMove()` call it:

```c
CoreStatus CoreMinimaxMove(
    const Board* board,
    const char* snake_id,
    int time_budget_ms,
    MoveDirection* out_move
) {
    CoreSearchConfig config = CoreSearchConfigDefault(time_budget_ms);
    CoreSearchStats stats;
    CoreSearchStatsInit(&stats);
    return CoreMinimaxMoveWithStats(board, snake_id, config, out_move, &stats);
}
```

`CoreMinimaxMoveWithStats()` must capture wall-clock bounds for `elapsed_ms`. The
existing `CoreSearchTimer` only stores a deadline, so add explicit start/end
timespecs: call `clock_gettime(CLOCK_MONOTONIC, &start_time)` before the
iterative-deepening loop and `clock_gettime(CLOCK_MONOTONIC, &end_time)` after it,
then set:

```c
stats->completed_depth = depth;
stats->max_depth_started = depth;
stats->timed_out = timed_out;
stats->score = score;
stats->move = completed_best;
stats->elapsed_ms = core_elapsed_ms(start_time, end_time);
```

Note `depth`, `timed_out`, `score`, and `completed_best` here refer to the
last-completed-iteration values tracked across the deepening loop (as in the
existing `CoreMinimaxMove()`), not the loop variable after it exits.

For fixed depth, use `config.fixed_depth > 0` as the maximum depth and require it to complete:

```c
int max_depth = config.fixed_depth > 0 ? config.fixed_depth : CORE_MINIMAX_MAX_DEPTH;
```

- [ ] **Step 7: Add Python wrapper**

In `battlesnake/c-core/py-core/py_core.c`, add helper:

```c
static int dict_set_u64(PyObject* dict, const char* key, uint64_t value) {
    PyObject* object = PyLong_FromUnsignedLongLong(value);
    if (object == NULL) {
        return -1;
    }
    int result = PyDict_SetItemString(dict, key, object);
    Py_DECREF(object);
    return result;
}
```

Add `py_minimax_diagnostics()`:

```c
static PyObject* py_minimax_diagnostics(PyObject* self, PyObject* args, PyObject* kwds) {
    (void)self;
    static char* kwlist[] = {"board", "snake_id", "time_budget_ms", "fixed_depth", "enable_tt", "enable_move_ordering", "enable_make_unmake", NULL};
    PyObject* board_obj = NULL;
    const char* snake_id = NULL;
    int time_budget_ms = 400;
    int fixed_depth = 0;
    int enable_tt = 1;
    int enable_move_ordering = 1;
    int enable_make_unmake = 1;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "Os|iiiii", kwlist, &board_obj, &snake_id, &time_budget_ms, &fixed_depth, &enable_tt, &enable_move_ordering, &enable_make_unmake)) {
        return NULL;
    }

    Board* board = board_from_pyobject(board_obj);
    if (board == NULL) {
        return NULL;
    }

    CoreSearchConfig config = CoreSearchConfigDefault(time_budget_ms);
    config.fixed_depth = fixed_depth;
    config.enable_tt = enable_tt != 0;
    config.enable_move_ordering = enable_move_ordering != 0;
    config.enable_make_unmake = enable_make_unmake != 0;

    MoveDirection out_move = MOVE_INVALID;
    CoreSearchStats stats;
    CoreSearchStatsInit(&stats);
    CoreStatus status = CoreMinimaxMoveWithStats(board, snake_id, config, &out_move, &stats);
    if (status != CORE_OK) {
        return raise_for_status(status);
    }

    PyObject* result = PyDict_New();
    if (result == NULL) {
        return NULL;
    }

    if (
        PyDict_SetItemString(result, "move", PyUnicode_FromString(MoveDirectionToString(out_move))) < 0 ||
        PyDict_SetItemString(result, "score", PyFloat_FromDouble(stats.score)) < 0 ||
        PyDict_SetItemString(result, "elapsed_ms", PyFloat_FromDouble(stats.elapsed_ms)) < 0 ||
        PyDict_SetItemString(result, "completed_depth", PyLong_FromLong(stats.completed_depth)) < 0 ||
        PyDict_SetItemString(result, "max_depth_started", PyLong_FromLong(stats.max_depth_started)) < 0 ||
        PyDict_SetItemString(result, "timed_out", PyBool_FromLong(stats.timed_out ? 1 : 0)) < 0 ||
        dict_set_u64(result, "nodes", stats.nodes) < 0 ||
        dict_set_u64(result, "leaf_evals", stats.leaf_evals) < 0 ||
        dict_set_u64(result, "clone_calls", stats.clone_calls) < 0 ||
        dict_set_u64(result, "board_allocations", stats.board_allocations) < 0 ||
        dict_set_u64(result, "safe_move_calls", stats.safe_move_calls) < 0 ||
        dict_set_u64(result, "beta_cutoffs", stats.beta_cutoffs) < 0 ||
        dict_set_u64(result, "move_order_first_choice_cutoffs", stats.move_order_first_choice_cutoffs) < 0 ||
        dict_set_u64(result, "tt_probes", stats.tt_probes) < 0 ||
        dict_set_u64(result, "tt_hits", stats.tt_hits) < 0 ||
        dict_set_u64(result, "tt_exact_hits", stats.tt_exact_hits) < 0 ||
        dict_set_u64(result, "tt_lower_hits", stats.tt_lower_hits) < 0 ||
        dict_set_u64(result, "tt_upper_hits", stats.tt_upper_hits) < 0 ||
        dict_set_u64(result, "tt_cutoffs", stats.tt_cutoffs) < 0 ||
        dict_set_u64(result, "tt_stores", stats.tt_stores) < 0 ||
        dict_set_u64(result, "tt_collisions", stats.tt_collisions) < 0
    ) {
        Py_DECREF(result);
        return NULL;
    }
    return result;
}
```

Add to `PyCoreMethods`:

```c
{"minimax_diagnostics", (PyCFunction)py_minimax_diagnostics, METH_VARARGS | METH_KEYWORDS, "Run minimax and return search diagnostics."},
```

- [ ] **Step 8: Add pyi signature**

Modify `battlesnake/battlesnake_native.pyi`:

```python
def minimax_diagnostics(
    board: Board,
    snake_id: str,
    time_budget_ms: int = 400,
    fixed_depth: int = 0,
    enable_tt: bool = True,
    enable_move_ordering: bool = True,
    enable_make_unmake: bool = True,
) -> dict[str, object]: ...
```

- [ ] **Step 9: Rebuild and run diagnostics tests**

Run:

```bash
python3 setup.py build_ext --inplace --force
python3 -m unittest tests.test_search_diagnostics -v
```

Expected:

```text
test_fixed_depth_diagnostics_are_deterministic_enough_for_regression_tests ... ok
test_minimax_diagnostics_shape ... ok
```

- [ ] **Step 10: Commit**

```bash
git add setup.py battlesnake/battlesnake_native.pyi battlesnake/c-core/core/core_algorithms.h battlesnake/c-core/core/core_algorithms.c battlesnake/c-core/core/search_stats.h battlesnake/c-core/core/search_stats.c battlesnake/c-core/py-core/py_core.c tests/test_search_diagnostics.py
git commit -m "feat: expose minimax search diagnostics"
```

---

### Task 3: Realistic Benchmark Runner And Baseline Report

**Files:**
- Create: `benchmarks/run_minimax_bench.py`
- Create: `benchmarks/compare_benchmarks.py`
- Create: `benchmarks/results/.gitkeep`
- Test: `tests/test_search_diagnostics.py`

- [ ] **Step 1: Create results directory marker**

Create `benchmarks/results/.gitkeep` as an empty file.

- [ ] **Step 2: Create benchmark runner**

Create `benchmarks/run_minimax_bench.py`:

```python
from __future__ import annotations

import argparse
import json
import statistics
import time
from pathlib import Path
from typing import Any

from battlesnake.battlesnake_native import minimax_diagnostics
from benchmarks.scenarios import SCENARIOS, build_board


def percentile(values: list[float], pct: float) -> float:
    ordered = sorted(values)
    if not ordered:
        return 0.0
    index = int(round((len(ordered) - 1) * pct))
    return ordered[index]


def summarize(rows: list[dict[str, Any]], scenario: str, budget_ms: int, fixed_depth: int, runs: int, warmup: int) -> dict[str, Any]:
    elapsed = [float(row["elapsed_ms"]) for row in rows]
    nodes = [int(row["nodes"]) for row in rows]
    leaf_evals = [int(row["leaf_evals"]) for row in rows]
    clone_calls = [int(row["clone_calls"]) for row in rows]
    board_allocations = [int(row["board_allocations"]) for row in rows]
    beta_cutoffs = [int(row["beta_cutoffs"]) for row in rows]
    first_choice_cutoffs = [int(row["move_order_first_choice_cutoffs"]) for row in rows]
    tt_probes = [int(row["tt_probes"]) for row in rows]
    tt_hits = [int(row["tt_hits"]) for row in rows]
    tt_cutoffs = [int(row["tt_cutoffs"]) for row in rows]
    tt_stores = [int(row["tt_stores"]) for row in rows]
    nodes_per_second = [
        (float(row["nodes"]) / max(float(row["elapsed_ms"]), 0.001)) * 1000.0
        for row in rows
    ]
    tt_hit_rates = [
        float(row["tt_hits"]) / float(row["tt_probes"])
        if int(row["tt_probes"]) > 0 else 0.0
        for row in rows
    ]

    return {
        "scenario": scenario,
        "budget_ms": budget_ms,
        "fixed_depth": fixed_depth,
        "runs": runs,
        "warmup": warmup,
        "move": rows[-1]["move"],
        "completed_depth": int(statistics.median(int(row["completed_depth"]) for row in rows)),
        "max_depth_started": max(int(row["max_depth_started"]) for row in rows),
        "timed_out_count": sum(1 for row in rows if bool(row["timed_out"])),
        "elapsed_ms_min": min(elapsed),
        "elapsed_ms_p50": percentile(elapsed, 0.50),
        "elapsed_ms_p95": percentile(elapsed, 0.95),
        "elapsed_ms_max": max(elapsed),
        "nodes_min": min(nodes),
        "nodes_p50": percentile([float(value) for value in nodes], 0.50),
        "nodes_max": max(nodes),
        "nodes_per_second_p50": percentile(nodes_per_second, 0.50),
        "leaf_evals_p50": percentile([float(value) for value in leaf_evals], 0.50),
        "clone_calls_p50": percentile([float(value) for value in clone_calls], 0.50),
        "board_allocations_p50": percentile([float(value) for value in board_allocations], 0.50),
        "beta_cutoffs_p50": percentile([float(value) for value in beta_cutoffs], 0.50),
        "move_order_first_choice_cutoffs_p50": percentile([float(value) for value in first_choice_cutoffs], 0.50),
        "tt_probes_p50": percentile([float(value) for value in tt_probes], 0.50),
        "tt_hits_p50": percentile([float(value) for value in tt_hits], 0.50),
        "tt_hit_rate_p50": percentile(tt_hit_rates, 0.50),
        "tt_cutoffs_p50": percentile([float(value) for value in tt_cutoffs], 0.50),
        "tt_stores_p50": percentile([float(value) for value in tt_stores], 0.50),
    }


def run_case(scenario_name: str, budget_ms: int, fixed_depth: int, runs: int, warmup: int) -> dict[str, Any]:
    scenario = next(item for item in SCENARIOS if item.name == scenario_name)
    rows: list[dict[str, Any]] = []
    for index in range(warmup + runs):
        board = build_board(scenario)
        result = minimax_diagnostics(
            board,
            scenario.snake_id,
            time_budget_ms=budget_ms,
            fixed_depth=fixed_depth,
            enable_tt=True,
            enable_move_ordering=True,
            enable_make_unmake=True,
        )
        if index >= warmup:
            rows.append(result)
    return summarize(rows, scenario_name, budget_ms, fixed_depth, runs, warmup)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--runs", type=int, default=20)
    parser.add_argument("--warmup", type=int, default=3)
    parser.add_argument("--budgets", default="180,320,450")
    parser.add_argument("--fixed-depths", default="0,4,6")
    parser.add_argument("--out", type=Path, default=Path("benchmarks/results/minimax-bench.jsonl"))
    args = parser.parse_args()

    budgets = [int(value) for value in args.budgets.split(",") if value]
    fixed_depths = [int(value) for value in args.fixed_depths.split(",") if value]
    args.out.parent.mkdir(parents=True, exist_ok=True)

    started = time.strftime("%Y-%m-%dT%H:%M:%S%z")
    with args.out.open("w", encoding="utf-8") as output:
        for scenario in SCENARIOS:
            for budget_ms in budgets:
                for fixed_depth in fixed_depths:
                    row = run_case(scenario.name, budget_ms, fixed_depth, args.runs, args.warmup)
                    row["started_at"] = started
                    output.write(json.dumps(row, sort_keys=True) + "\n")
                    output.flush()
                    print(json.dumps(row, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

- [ ] **Step 3: Create benchmark comparator**

Create `benchmarks/compare_benchmarks.py`:

```python
from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


def load(path: Path) -> dict[tuple[str, int, int], dict[str, Any]]:
    result: dict[tuple[str, int, int], dict[str, Any]] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        row = json.loads(line)
        key = (str(row["scenario"]), int(row["budget_ms"]), int(row["fixed_depth"]))
        result[key] = row
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("baseline", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--max-p50-regression", type=float, default=1.08)
    args = parser.parse_args()

    baseline = load(args.baseline)
    candidate = load(args.candidate)
    failed = False

    for key in sorted(baseline):
        if key not in candidate:
            print(f"missing candidate row: {key}")
            failed = True
            continue
        base = baseline[key]
        cand = candidate[key]
        base_ms = float(base["elapsed_ms_p50"])
        cand_ms = float(cand["elapsed_ms_p50"])
        ratio = cand_ms / max(base_ms, 0.001)
        base_depth = int(base["completed_depth"])
        cand_depth = int(cand["completed_depth"])
        print(f"{key}: elapsed_p50 {base_ms:.3f} -> {cand_ms:.3f} ms ({ratio:.3f}x), depth {base_depth} -> {cand_depth}")
        if ratio > args.max_p50_regression and cand_depth <= base_depth:
            failed = True

    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
```

- [ ] **Step 4: Run a short benchmark smoke**

Run:

```bash
python3 -m benchmarks.run_minimax_bench --runs 2 --warmup 1 --budgets 25 --fixed-depths 0,3 --out benchmarks/results/smoke.jsonl
```

Expected:

```text
{"budget_ms": 25, "clone_calls_p50": 123.0, "completed_depth": 3, "scenario": "duel_open_7x7"}
```

The output must include one JSON line for each scenario and each fixed-depth mode.

- [ ] **Step 5: Capture baseline benchmark**

Run:

```bash
python3 -m benchmarks.run_minimax_bench --runs 20 --warmup 3 --budgets 180,320,450 --fixed-depths 0,4,6 --out benchmarks/results/baseline-before-tt.jsonl
```

Expected:

```text
benchmarks/results/baseline-before-tt.jsonl
```

contains 72 JSONL rows: 8 scenarios × 3 budgets × 3 fixed-depth modes.

- [ ] **Step 6: Commit**

```bash
git add benchmarks/run_minimax_bench.py benchmarks/compare_benchmarks.py benchmarks/results/.gitkeep
git commit -m "bench: add realistic minimax benchmark runner"
```

---

### Task 4: Zobrist Hashing

**Files:**
- Create: `battlesnake/c-core/core/zobrist.h`
- Create: `battlesnake/c-core/core/zobrist.c`
- Create: `tests/test_zobrist_hash.py`
- Modify: `setup.py`

- [ ] **Step 1: Write hash behavior tests**

Create `tests/test_zobrist_hash.py`:

```python
from __future__ import annotations

import unittest

from battlesnake.battlesnake_native import board_hash
from benchmarks.scenarios import build_board, get_scenario


class ZobristHashTests(unittest.TestCase):
    def test_same_board_has_same_hash(self) -> None:
        scenario = get_scenario("duel_center_pressure_11x11")
        first = board_hash(build_board(scenario))
        second = board_hash(build_board(scenario))
        self.assertEqual(first, second)

    def test_different_positions_change_hash(self) -> None:
        scenario = get_scenario("duel_open_7x7")
        board = build_board(scenario)
        moved = board.clone_and_apply({"me": "up", "you": "up"})
        self.assertNotEqual(board_hash(board), board_hash(moved))


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run test and verify failure**

Run:

```bash
python3 -m unittest tests.test_zobrist_hash -v
```

Expected failure:

```text
ImportError: cannot import name 'board_hash'
```

- [ ] **Step 3: Add Zobrist C API**

Create `battlesnake/c-core/core/zobrist.h`:

```c
#pragma once

#include <stdint.h>

#include "../datatypes/board.h"

uint64_t CoreZobristHashBoard(const Board* board);
uint64_t CoreZobristHashMove(uint64_t hash, int snake_index, Coord old_head, Coord new_head, MoveDirection move);
```

Create `battlesnake/c-core/core/zobrist.c`:

```c
#include "zobrist.h"

#include <stdint.h>
#include <string.h>

static uint64_t mix64(uint64_t value) {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

static uint64_t string_hash(const char* value) {
    uint64_t hash = 1469598103934665603ULL;
    if (value == NULL) {
        return hash;
    }
    for (const unsigned char* cursor = (const unsigned char*)value; *cursor != '\0'; cursor++) {
        hash ^= (uint64_t)(*cursor);
        hash *= 1099511628211ULL;
    }
    return hash;
}

static uint64_t coord_key(int domain, int index, Coord coord, int extra) {
    uint64_t value = (uint64_t)(uint32_t)coord.x;
    value |= ((uint64_t)(uint32_t)coord.y) << 16;
    value |= ((uint64_t)(uint32_t)index) << 32;
    value |= ((uint64_t)(uint32_t)domain) << 48;
    value ^= ((uint64_t)(uint32_t)extra) * 0x517cc1b727220a95ULL;
    return mix64(value);
}

uint64_t CoreZobristHashBoard(const Board* board) {
    if (board == NULL) {
        return 0;
    }

    uint64_t hash = mix64((uint64_t)(uint32_t)board->width | ((uint64_t)(uint32_t)board->height << 32));
    hash ^= mix64(string_hash(board->ruleset_name));
    hash ^= mix64((uint64_t)(uint32_t)board->hazard_damage << 1);

    for (int i = 0; i < board->snake_count; i++) {
        const Snake* snake = &board->snakes[i];
        hash ^= mix64(string_hash(snake->id) ^ ((uint64_t)(uint32_t)i << 32));
        hash ^= mix64(((uint64_t)(uint32_t)snake->health << 8) ^ (uint64_t)(uint32_t)snake->length);
        for (int j = 0; j < snake->body_len; j++) {
            hash ^= coord_key(1, i, snake->body[j], j);
        }
    }

    for (int i = 0; i < board->food_count; i++) {
        hash ^= coord_key(2, i, board->food[i], 0);
    }
    for (int i = 0; i < board->hazard_count; i++) {
        hash ^= coord_key(3, i, board->hazards[i], board->hazard_damage);
    }
    return hash;
}

uint64_t CoreZobristHashMove(uint64_t hash, int snake_index, Coord old_head, Coord new_head, MoveDirection move) {
    hash ^= coord_key(4, snake_index, old_head, (int)move);
    hash ^= coord_key(4, snake_index, new_head, (int)move);
    return hash;
}
```

- [ ] **Step 4: Add wrapper `board_hash()`**

Add source file to `setup.py`:

```python
"battlesnake/c-core/core/zobrist.c",
```

In `py_core.c`, add:

```c
#include "../core/zobrist.h"
```

Add wrapper:

```c
static PyObject* py_board_hash(PyObject* self, PyObject* args) {
    (void)self;
    PyObject* board_obj = NULL;
    if (!PyArg_ParseTuple(args, "O", &board_obj)) {
        return NULL;
    }
    Board* board = board_from_pyobject(board_obj);
    if (board == NULL) {
        return NULL;
    }
    return PyLong_FromUnsignedLongLong(CoreZobristHashBoard(board));
}
```

Add method:

```c
{"board_hash", py_board_hash, METH_VARARGS, "Return deterministic 64-bit Zobrist hash for a board."},
```

Add pyi:

```python
def board_hash(board: Board) -> int: ...
```

- [ ] **Step 5: Rebuild and run tests**

Run:

```bash
python3 setup.py build_ext --inplace --force
python3 -m unittest tests.test_zobrist_hash -v
```

Expected:

```text
test_different_positions_change_hash ... ok
test_same_board_has_same_hash ... ok
```

- [ ] **Step 6: Commit**

```bash
git add setup.py battlesnake/battlesnake_native.pyi battlesnake/c-core/core/zobrist.h battlesnake/c-core/core/zobrist.c battlesnake/c-core/py-core/py_core.c tests/test_zobrist_hash.py
git commit -m "feat: add deterministic board zobrist hashing"
```

---

### Task 5: Transposition Table

**Files:**
- Create: `battlesnake/c-core/core/transposition_table.h`
- Create: `battlesnake/c-core/core/transposition_table.c`
- Modify: `setup.py`
- Modify: `battlesnake/c-core/core/core_algorithms.c`
- Modify: `battlesnake/c-core/core/search_stats.h`
- Test: `tests/test_search_diagnostics.py`

- [ ] **Step 1: Extend diagnostics test for TT metrics**

Append to `tests/test_search_diagnostics.py`:

```python
    def test_tt_metrics_are_reported_when_enabled(self) -> None:
        scenario = get_scenario("duel_late_game_long_bodies")
        board = build_board(scenario)
        result = minimax_diagnostics(board, scenario.snake_id, time_budget_ms=1000, fixed_depth=4, enable_tt=True)

        self.assertGreaterEqual(result["tt_probes"], 0)
        self.assertGreaterEqual(result["tt_hits"], 0)
        self.assertGreaterEqual(result["tt_stores"], 0)
        self.assertGreater(result["tt_stores"], 0)
```

- [ ] **Step 2: Run test and verify failure**

Run:

```bash
python3 -m unittest tests.test_search_diagnostics.SearchDiagnosticsTests.test_tt_metrics_are_reported_when_enabled -v
```

Expected failure:

```text
AssertionError: 0 not greater than 0
```

- [ ] **Step 3: Create TT module**

Create `battlesnake/c-core/core/transposition_table.h`:

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "../datatypes/board.h"

typedef enum {
    CORE_TT_EXACT = 0,
    CORE_TT_LOWER = 1,
    CORE_TT_UPPER = 2,
} CoreTtBound;

typedef struct {
    uint64_t hash;
    double score;
    int depth;
    int generation;
    CoreTtBound bound;
    MoveDirection best_move;
    bool occupied;
} CoreTtEntry;

typedef struct {
    CoreTtEntry* entries;
    size_t capacity;
    int generation;
} CoreTranspositionTable;

bool CoreTtInit(CoreTranspositionTable* table, size_t capacity);
void CoreTtFree(CoreTranspositionTable* table);
void CoreTtNextGeneration(CoreTranspositionTable* table);
bool CoreTtProbe(const CoreTranspositionTable* table, uint64_t hash, int depth, double alpha, double beta, double* out_score, MoveDirection* out_best_move, CoreTtBound* out_bound);
void CoreTtStore(CoreTranspositionTable* table, uint64_t hash, int depth, double score, CoreTtBound bound, MoveDirection best_move);
```

Create `battlesnake/c-core/core/transposition_table.c`:

```c
#include "transposition_table.h"

#include <stdlib.h>
#include <string.h>

bool CoreTtInit(CoreTranspositionTable* table, size_t capacity) {
    if (table == NULL || capacity == 0) {
        return false;
    }
    table->entries = (CoreTtEntry*)calloc(capacity, sizeof(CoreTtEntry));
    if (table->entries == NULL) {
        return false;
    }
    table->capacity = capacity;
    table->generation = 1;
    return true;
}

void CoreTtFree(CoreTranspositionTable* table) {
    if (table == NULL) {
        return;
    }
    free(table->entries);
    table->entries = NULL;
    table->capacity = 0;
    table->generation = 0;
}

void CoreTtNextGeneration(CoreTranspositionTable* table) {
    if (table == NULL) {
        return;
    }
    table->generation++;
    if (table->generation <= 0) {
        table->generation = 1;
        memset(table->entries, 0, table->capacity * sizeof(CoreTtEntry));
    }
}

static size_t table_index(const CoreTranspositionTable* table, uint64_t hash) {
    return (size_t)(hash % (uint64_t)table->capacity);
}

bool CoreTtProbe(const CoreTranspositionTable* table, uint64_t hash, int depth, double alpha, double beta, double* out_score, MoveDirection* out_best_move, CoreTtBound* out_bound) {
    if (table == NULL || table->entries == NULL || table->capacity == 0) {
        return false;
    }
    const CoreTtEntry* entry = &table->entries[table_index(table, hash)];
    if (!entry->occupied || entry->hash != hash) {
        return false;
    }
    if (out_best_move != NULL) {
        *out_best_move = entry->best_move;
    }
    if (out_bound != NULL) {
        *out_bound = entry->bound;
    }
    if (entry->depth < depth) {
        return false;
    }
    if (entry->bound == CORE_TT_EXACT || (entry->bound == CORE_TT_LOWER && entry->score >= beta) || (entry->bound == CORE_TT_UPPER && entry->score <= alpha)) {
        if (out_score != NULL) {
            *out_score = entry->score;
        }
        return true;
    }
    return false;
}

void CoreTtStore(CoreTranspositionTable* table, uint64_t hash, int depth, double score, CoreTtBound bound, MoveDirection best_move) {
    if (table == NULL || table->entries == NULL || table->capacity == 0) {
        return;
    }
    CoreTtEntry* entry = &table->entries[table_index(table, hash)];
    if (entry->occupied && entry->generation == table->generation && entry->depth > depth) {
        return;
    }
    entry->occupied = true;
    entry->hash = hash;
    entry->score = score;
    entry->depth = depth;
    entry->generation = table->generation;
    entry->bound = bound;
    entry->best_move = best_move;
}
```

- [ ] **Step 4: Integrate TT into search context**

Add source file to `setup.py`:

```python
"battlesnake/c-core/core/transposition_table.c",
```

In `core_algorithms.c`, include:

```c
#include "transposition_table.h"
#include "zobrist.h"
```

Create a `CoreSearchContext` local struct near minimax helpers:

```c
typedef struct {
    CoreSearchTimer timer;
    CoreSearchConfig config;
    CoreSearchStats* stats;
    CoreTranspositionTable tt;
    bool tt_enabled;
} CoreSearchContext;
```

Replace recursive parameters `timer` and `stats` with `CoreSearchContext* context`.

At node entry:

```c
uint64_t hash = CoreZobristHashBoard(board) ^ ((uint64_t)(uint32_t)depth << 48);
MoveDirection tt_best_move = MOVE_INVALID;
if (context->tt_enabled) {
    context->stats->tt_probes++;
    double tt_score = 0.0;
    CoreTtBound tt_bound = CORE_TT_EXACT;
    if (CoreTtProbe(&context->tt, hash, depth, alpha, beta, &tt_score, &tt_best_move, &tt_bound)) {
        context->stats->tt_hits++;
        context->stats->tt_cutoffs++;
        *out_score = tt_score;
        if (out_best_move != NULL) {
            *out_best_move = tt_best_move;
        }
        return CORE_OK;
    }
}
```

Before returning a completed node. Note the existing search mutates `alpha` inside
the move loop (`alpha = best_score;`), so capture the node's incoming alpha as
`double original_alpha = alpha;` at node entry (before the loop) and use that for
the bound classification below:

```c
if (context->tt_enabled && !*timed_out) {
    CoreTtBound bound = CORE_TT_EXACT;
    if (best_score <= original_alpha) {
        bound = CORE_TT_UPPER;
    } else if (best_score >= beta) {
        bound = CORE_TT_LOWER;
    }
    CoreTtStore(&context->tt, hash, depth, best_score, bound, best_move);
    context->stats->tt_stores++;
}
```

Initialize table in `CoreMinimaxMoveWithStats()`:

```c
CoreSearchContext context;
context.timer = core_search_timer_start(config.time_budget_ms);
context.config = config;
context.stats = out_stats;
context.tt_enabled = config.enable_tt && CoreTtInit(&context.tt, 1u << 20);
```

Free at exit:

```c
if (context.tt_enabled) {
    CoreTtFree(&context.tt);
}
```

- [ ] **Step 5: Rebuild and run TT test**

Run:

```bash
python3 setup.py build_ext --inplace --force
python3 -m unittest tests.test_search_diagnostics.SearchDiagnosticsTests.test_tt_metrics_are_reported_when_enabled -v
```

Expected:

```text
test_tt_metrics_are_reported_when_enabled ... ok
```

- [ ] **Step 6: Run before/after benchmark comparison**

Run:

```bash
python3 -m benchmarks.run_minimax_bench --runs 20 --warmup 3 --budgets 180,320,450 --fixed-depths 0,4,6 --out benchmarks/results/after-tt.jsonl
python3 -m benchmarks.compare_benchmarks benchmarks/results/baseline-before-tt.jsonl benchmarks/results/after-tt.jsonl
```

Expected: comparator exits 0 and TT rows show non-zero `tt_stores_p50`; dense scenarios should show non-zero `tt_hit_rate_p50`.

- [ ] **Step 7: Commit**

```bash
git add setup.py battlesnake/c-core/core/core_algorithms.c battlesnake/c-core/core/search_stats.h battlesnake/c-core/core/transposition_table.h battlesnake/c-core/core/transposition_table.c tests/test_search_diagnostics.py
git commit -m "feat: add minimax transposition table"
```

---

### Task 6: Move Ordering Beyond Root

**Files:**
- Modify: `battlesnake/c-core/core/core_algorithms.c`
- Modify: `battlesnake/c-core/core/search_stats.h`
- Test: `tests/test_search_diagnostics.py`

- [ ] **Step 1: Add ordering regression test**

Append to `tests/test_search_diagnostics.py`:

```python
    def test_move_ordering_changes_cutoff_profile_without_changing_move(self) -> None:
        scenario = get_scenario("duel_center_pressure_11x11")
        board = build_board(scenario)
        ordered = minimax_diagnostics(board, scenario.snake_id, time_budget_ms=1000, fixed_depth=5, enable_move_ordering=True)
        unordered = minimax_diagnostics(board, scenario.snake_id, time_budget_ms=1000, fixed_depth=5, enable_move_ordering=False)

        self.assertEqual(ordered["move"], unordered["move"])
        self.assertLessEqual(ordered["nodes"], unordered["nodes"])
        ordered_cutoff_rate = ordered["move_order_first_choice_cutoffs"] / max(ordered["beta_cutoffs"], 1)
        unordered_cutoff_rate = unordered["move_order_first_choice_cutoffs"] / max(unordered["beta_cutoffs"], 1)
        self.assertGreaterEqual(ordered_cutoff_rate, unordered_cutoff_rate)
```

- [ ] **Step 2: Run test and observe current weak ordering**

Run:

```bash
python3 -m unittest tests.test_search_diagnostics.SearchDiagnosticsTests.test_move_ordering_changes_cutoff_profile_without_changing_move -v
```

Expected: it may pass or fail before implementation. If it passes, keep it as a regression guard and continue because current ordering is still root-heavy.

- [ ] **Step 3: Add ordering state**

In `core_algorithms.c`, add to `CoreSearchContext`:

```c
MoveDirection principal_variation[CORE_MINIMAX_MAX_DEPTH + 1];
MoveDirection killer_moves[CORE_MINIMAX_MAX_DEPTH + 1][2];
int history_scores[4];
```

Initialize all moves to `MOVE_INVALID` and history scores to zero in `CoreMinimaxMoveWithStats()`.

- [ ] **Step 4: Replace single preferred move with scored move ordering**

Add helper:

```c
static int core_move_order_score(CoreSearchContext* context, int ply, MoveDirection move, MoveDirection tt_best, MoveDirection previous_iteration_best) {
    int score = 0;
    if (move == tt_best) {
        score += 100000;
    }
    if (move == previous_iteration_best) {
        score += 50000;
    }
    if (move == context->principal_variation[ply]) {
        score += 25000;
    }
    if (move == context->killer_moves[ply][0]) {
        score += 12000;
    }
    if (move == context->killer_moves[ply][1]) {
        score += 8000;
    }
    if (move >= MOVE_UP && move <= MOVE_RIGHT) {
        score += context->history_scores[(int)move];
    }
    return score;
}
```

Add helper:

```c
static void core_order_moves(CoreSearchContext* context, int ply, MoveDirection* moves, int move_count, MoveDirection tt_best, MoveDirection previous_iteration_best) {
    if (!context->config.enable_move_ordering) {
        return;
    }
    for (int i = 1; i < move_count; i++) {
        MoveDirection move = moves[i];
        int score = core_move_order_score(context, ply, move, tt_best, previous_iteration_best);
        int j = i - 1;
        while (j >= 0 && core_move_order_score(context, ply, moves[j], tt_best, previous_iteration_best) < score) {
            moves[j + 1] = moves[j];
            j--;
        }
        moves[j + 1] = move;
    }
}
```

Call it after safe moves are produced at every recursive node:

```c
core_order_moves(context, ply, own_moves, own_move_count, tt_best_move, preferred_move);
```

- [ ] **Step 5: Update killer and history data on beta cutoffs**

When a move causes `worst_reply <= alpha`:

```c
if (own_move != context->killer_moves[ply][0]) {
    context->killer_moves[ply][1] = context->killer_moves[ply][0];
    context->killer_moves[ply][0] = own_move;
}
if (own_move >= MOVE_UP && own_move <= MOVE_RIGHT) {
    context->history_scores[(int)own_move] += depth * depth;
}
```

When a completed depth returns a root candidate:

```c
context.principal_variation[0] = candidate;
```

- [ ] **Step 6: Rebuild, test, and benchmark**

Run:

```bash
python3 setup.py build_ext --inplace --force
python3 -m unittest tests.test_search_diagnostics -v
python3 -m benchmarks.run_minimax_bench --runs 20 --warmup 3 --budgets 180,320,450 --fixed-depths 0,4,6 --out benchmarks/results/after-ordering.jsonl
python3 -m benchmarks.compare_benchmarks benchmarks/results/after-tt.jsonl benchmarks/results/after-ordering.jsonl
```

Expected: tests pass; comparator exits 0; fixed-depth move ordering reduces `nodes_p50` in at least `duel_corridor_choke` or `duel_late_game_long_bodies` while preserving moves. `move_order_first_choice_cutoffs_p50` should remain reported, but do not require its raw count or rate to increase on every scenario because successful ordering can reduce the number and distribution of visited cutoff nodes.

- [ ] **Step 7: Commit**

```bash
git add battlesnake/c-core/core/core_algorithms.c battlesnake/c-core/core/search_stats.h tests/test_search_diagnostics.py
git commit -m "feat: improve minimax move ordering"
```

---

### Task 7: Reusable Search Workspace And Occupancy Bitmap

**Files:**
- Create: `battlesnake/c-core/core/search_workspace.h`
- Create: `battlesnake/c-core/core/search_workspace.c`
- Modify: `setup.py`
- Modify: `battlesnake/c-core/core/core_algorithms.c`
- Modify: `battlesnake/c-core/core/search_stats.h`
- Test: `tests/test_search_diagnostics.py`

- [ ] **Step 1: Add allocation-ratio regression test**

Append to `tests/test_search_diagnostics.py`:

```python
    def test_allocation_counters_are_low_enough_for_fixed_depth_after_workspace(self) -> None:
        scenario = get_scenario("duel_open_7x7")
        board = build_board(scenario)
        result = minimax_diagnostics(board, scenario.snake_id, time_budget_ms=1000, fixed_depth=4)

        self.assertGreater(result["nodes"], 0)
        self.assertLessEqual(result["board_allocations"], result["nodes"])
```

- [ ] **Step 2: Create workspace API**

Create `battlesnake/c-core/core/search_workspace.h`:

```c
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "../datatypes/board.h"

typedef struct {
    unsigned char* occupied;
    size_t occupied_count;
    const char** snake_ids;
    MoveDirection* moves;
    int* option_counts;
    MoveDirection (*options)[4];
    int snake_capacity;
} CoreSearchWorkspace;

bool CoreSearchWorkspaceInit(CoreSearchWorkspace* workspace, int max_snakes, size_t cell_count);
void CoreSearchWorkspaceFree(CoreSearchWorkspace* workspace);
bool CoreSearchWorkspaceEnsure(CoreSearchWorkspace* workspace, int max_snakes, size_t cell_count);
void CoreSearchWorkspaceFillOccupied(CoreSearchWorkspace* workspace, const Board* board, bool include_tails);
```

Create `battlesnake/c-core/core/search_workspace.c`:

```c
#include "search_workspace.h"

#include <stdlib.h>
#include <string.h>

static int coord_index(const Board* board, Coord coord) {
    return coord.y * board->width + coord.x;
}

bool CoreSearchWorkspaceInit(CoreSearchWorkspace* workspace, int max_snakes, size_t cell_count) {
    if (workspace == NULL) {
        return false;
    }
    memset(workspace, 0, sizeof(*workspace));
    return CoreSearchWorkspaceEnsure(workspace, max_snakes, cell_count);
}

void CoreSearchWorkspaceFree(CoreSearchWorkspace* workspace) {
    if (workspace == NULL) {
        return;
    }
    free(workspace->occupied);
    free(workspace->snake_ids);
    free(workspace->moves);
    free(workspace->option_counts);
    free(workspace->options);
    memset(workspace, 0, sizeof(*workspace));
}

bool CoreSearchWorkspaceEnsure(CoreSearchWorkspace* workspace, int max_snakes, size_t cell_count) {
    if (workspace == NULL || max_snakes < 0) {
        return false;
    }
    if (cell_count > workspace->occupied_count) {
        unsigned char* occupied = (unsigned char*)realloc(workspace->occupied, cell_count * sizeof(unsigned char));
        if (occupied == NULL) {
            return false;
        }
        workspace->occupied = occupied;
        workspace->occupied_count = cell_count;
    }
    if (max_snakes > workspace->snake_capacity) {
        const char** snake_ids = (const char**)realloc(workspace->snake_ids, (size_t)max_snakes * sizeof(char*));
        MoveDirection* moves = (MoveDirection*)realloc(workspace->moves, (size_t)max_snakes * sizeof(MoveDirection));
        int* option_counts = (int*)realloc(workspace->option_counts, (size_t)max_snakes * sizeof(int));
        MoveDirection (*options)[4] = (MoveDirection (*)[4])realloc(workspace->options, (size_t)max_snakes * sizeof(MoveDirection[4]));
        if (snake_ids == NULL || moves == NULL || option_counts == NULL || options == NULL) {
            free(snake_ids);
            free(moves);
            free(option_counts);
            free(options);
            return false;
        }
        workspace->snake_ids = snake_ids;
        workspace->moves = moves;
        workspace->option_counts = option_counts;
        workspace->options = options;
        workspace->snake_capacity = max_snakes;
    }
    return true;
}

void CoreSearchWorkspaceFillOccupied(CoreSearchWorkspace* workspace, const Board* board, bool include_tails) {
    memset(workspace->occupied, 0, workspace->occupied_count * sizeof(unsigned char));
    for (int i = 0; i < board->snake_count; i++) {
        const Snake* snake = &board->snakes[i];
        int limit = snake->body_len;
        if (!include_tails && limit > 0) {
            limit--;
        }
        for (int j = 0; j < limit; j++) {
            Coord coord = snake->body[j];
            if (BoardInBounds(board, coord)) {
                workspace->occupied[coord_index(board, coord)] = 1;
            }
        }
    }
}
```

- [ ] **Step 3: Add source and use workspace in minimax**

Add to `setup.py`:

```python
"battlesnake/c-core/core/search_workspace.c",
```

Add to `CoreSearchContext`:

```c
CoreSearchWorkspace workspace;
```

Initialize once before iterative deepening:

```c
size_t cell_count = (size_t)board->width * (size_t)board->height;
if (!CoreSearchWorkspaceInit(&context.workspace, board->snake_count, cell_count)) {
    if (context.tt_enabled) {
        CoreTtFree(&context.tt);
    }
    return CORE_ERROR;
}
```

Replace per-node `malloc()` arrays in `core_minimax_search()` with workspace arrays:

```c
const char** ids = context->workspace.snake_ids;
MoveDirection* moves = context->workspace.moves;
int* option_counts = context->workspace.option_counts;
MoveDirection (*options)[4] = context->workspace.options;
```

Remove the matching per-node `free()` calls.

Free at function exit:

```c
CoreSearchWorkspaceFree(&context.workspace);
```

- [ ] **Step 4: Rebuild, test, and benchmark**

Run:

```bash
python3 setup.py build_ext --inplace --force
python3 -m unittest tests.test_search_diagnostics -v
python3 -m benchmarks.run_minimax_bench --runs 20 --warmup 3 --budgets 180,320,450 --fixed-depths 0,4,6 --out benchmarks/results/after-workspace.jsonl
python3 -m benchmarks.compare_benchmarks benchmarks/results/after-ordering.jsonl benchmarks/results/after-workspace.jsonl
```

Expected: tests pass; comparator exits 0; `board_allocations_p50` does not increase for rows that complete the requested fixed depth; fixed-depth moves and depths remain stable. Timeout/budget rows may report more board allocations when they visit more nodes before the deadline. Treat fixed-depth elapsed p50 as advisory on this host because sub-millisecond and low-millisecond rows show scheduler noise.

- [ ] **Step 5: Commit**

```bash
git add setup.py battlesnake/c-core/core/core_algorithms.c battlesnake/c-core/core/search_stats.h battlesnake/c-core/core/search_workspace.h battlesnake/c-core/core/search_workspace.c tests/test_search_diagnostics.py
git commit -m "perf: reuse minimax search workspace"
```

---

### Task 8: In-Place Make/Unmake For Search

**Files:**
- Create: `battlesnake/c-core/core/search_state.h`
- Create: `battlesnake/c-core/core/search_state.c`
- Modify: `setup.py`
- Modify: `battlesnake/c-core/core/core_algorithms.c`
- Modify: `battlesnake/c-core/core/search_stats.h`
- Test: `tests/test_search_diagnostics.py`

- [ ] **Step 1: Add make/unmake equivalence test**

Append to `tests/test_search_diagnostics.py`:

```python
    def test_make_unmake_matches_clone_search_result(self) -> None:
        for name in ("duel_open_7x7", "duel_tail_chase_trap", "royale_hazard_ring_duel"):
            with self.subTest(scenario=name):
                scenario = get_scenario(name)
                board = build_board(scenario)
                clone_result = minimax_diagnostics(
                    board,
                    scenario.snake_id,
                    time_budget_ms=1000,
                    fixed_depth=4,
                    enable_make_unmake=False,
                )
                in_place_result = minimax_diagnostics(
                    board,
                    scenario.snake_id,
                    time_budget_ms=1000,
                    fixed_depth=4,
                    enable_make_unmake=True,
                )
                self.assertEqual(in_place_result["move"], clone_result["move"])
                self.assertAlmostEqual(float(in_place_result["score"]), float(clone_result["score"]), places=6)
                self.assertLessEqual(in_place_result["clone_calls"], clone_result["clone_calls"])
```

- [ ] **Step 2: Create search state API**

Create `battlesnake/c-core/core/search_state.h`:

```c
#pragma once

#include <stdbool.h>

#include "../datatypes/board.h"

typedef struct {
    Snake* snakes;
    int snake_count;
    Coord* food;
    int food_count;
    Coord* hazards;
    int hazard_count;
} CoreUndoBoardSnapshot;

typedef struct {
    Board board;
    CoreUndoBoardSnapshot* undo_stack;
    int undo_count;
    int undo_capacity;
} CoreSearchState;

bool CoreSearchStateInit(CoreSearchState* state, const Board* board);
void CoreSearchStateFree(CoreSearchState* state);
bool CoreSearchStateMakeMoves(CoreSearchState* state, const char** snake_ids, const MoveDirection* moves, int move_count);
bool CoreSearchStateUnmake(CoreSearchState* state);
const Board* CoreSearchStateBoard(const CoreSearchState* state);
```

Create `battlesnake/c-core/core/search_state.c`:

```c
#include "search_state.h"

#include <stdlib.h>
#include <string.h>

static void board_clear_dynamic(Board* board, bool free_ruleset) {
    if (board == NULL) {
        return;
    }
    for (int i = 0; i < board->snake_count; i++) {
        SnakeFree(&board->snakes[i]);
    }
    free(board->snakes);
    free(board->food);
    free(board->hazards);
    if (free_ruleset) {
        free(board->ruleset_name);
        board->ruleset_name = NULL;
    }
    board->snakes = NULL;
    board->snake_count = 0;
    board->food = NULL;
    board->food_count = 0;
    board->hazards = NULL;
    board->hazard_count = 0;
}

static void snapshot_free(CoreUndoBoardSnapshot* snapshot) {
    if (snapshot == NULL) {
        return;
    }
    for (int i = 0; i < snapshot->snake_count; i++) {
        SnakeFree(&snapshot->snakes[i]);
    }
    free(snapshot->snakes);
    free(snapshot->food);
    free(snapshot->hazards);
    memset(snapshot, 0, sizeof(*snapshot));
}

static bool snapshot_from_board(CoreUndoBoardSnapshot* snapshot, const Board* board) {
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->snake_count = board->snake_count;
    snapshot->food_count = board->food_count;
    snapshot->hazard_count = board->hazard_count;
    snapshot->snakes = (Snake*)calloc((size_t)board->snake_count, sizeof(Snake));
    snapshot->food = (Coord*)malloc((size_t)board->food_count * sizeof(Coord));
    snapshot->hazards = (Coord*)malloc((size_t)board->hazard_count * sizeof(Coord));
    if ((board->snake_count > 0 && snapshot->snakes == NULL) || (board->food_count > 0 && snapshot->food == NULL) || (board->hazard_count > 0 && snapshot->hazards == NULL)) {
        snapshot_free(snapshot);
        return false;
    }
    for (int i = 0; i < board->snake_count; i++) {
        SnakeCopy(&snapshot->snakes[i], &board->snakes[i]);
    }
    if (board->food_count > 0) {
        memcpy(snapshot->food, board->food, (size_t)board->food_count * sizeof(Coord));
    }
    if (board->hazard_count > 0) {
        memcpy(snapshot->hazards, board->hazards, (size_t)board->hazard_count * sizeof(Coord));
    }
    return true;
}

bool CoreSearchStateInit(CoreSearchState* state, const Board* board) {
    if (state == NULL || board == NULL) {
        return false;
    }
    memset(state, 0, sizeof(*state));
    Board* copy = BoardCopy(board);
    if (copy == NULL) {
        return false;
    }
    state->board = *copy;
    free(copy);
    return true;
}

void CoreSearchStateFree(CoreSearchState* state) {
    if (state == NULL) {
        return;
    }
    board_clear_dynamic(&state->board, true);
    for (int i = 0; i < state->undo_count; i++) {
        snapshot_free(&state->undo_stack[i]);
    }
    free(state->undo_stack);
    memset(state, 0, sizeof(*state));
}

bool CoreSearchStateMakeMoves(CoreSearchState* state, const char** snake_ids, const MoveDirection* moves, int move_count) {
    if (state == NULL) {
        return false;
    }
    if (state->undo_count == state->undo_capacity) {
        int next_capacity = state->undo_capacity == 0 ? 32 : state->undo_capacity * 2;
        CoreUndoBoardSnapshot* next_stack = (CoreUndoBoardSnapshot*)realloc(state->undo_stack, (size_t)next_capacity * sizeof(CoreUndoBoardSnapshot));
        if (next_stack == NULL) {
            return false;
        }
        state->undo_stack = next_stack;
        state->undo_capacity = next_capacity;
    }
    CoreUndoBoardSnapshot* snapshot = &state->undo_stack[state->undo_count];
    if (!snapshot_from_board(snapshot, &state->board)) {
        return false;
    }
    Board* next = BoardCloneAndApply(&state->board, snake_ids, moves, move_count);
    if (next == NULL) {
        snapshot_free(snapshot);
        return false;
    }
    board_clear_dynamic(&state->board, false);
    state->board.snakes = next->snakes;
    state->board.snake_count = next->snake_count;
    state->board.food = next->food;
    state->board.food_count = next->food_count;
    state->board.hazards = next->hazards;
    state->board.hazard_count = next->hazard_count;
    next->snakes = NULL;
    next->snake_count = 0;
    next->food = NULL;
    next->food_count = 0;
    next->hazards = NULL;
    next->hazard_count = 0;
    BoardFree(next);
    state->undo_count++;
    return true;
}

bool CoreSearchStateUnmake(CoreSearchState* state) {
    if (state == NULL || state->undo_count <= 0) {
        return false;
    }
    CoreUndoBoardSnapshot* snapshot = &state->undo_stack[state->undo_count - 1];
    board_clear_dynamic(&state->board, false);
    state->board.snakes = snapshot->snakes;
    state->board.snake_count = snapshot->snake_count;
    state->board.food = snapshot->food;
    state->board.food_count = snapshot->food_count;
    state->board.hazards = snapshot->hazards;
    state->board.hazard_count = snapshot->hazard_count;
    snapshot->snakes = NULL;
    snapshot->food = NULL;
    snapshot->hazards = NULL;
    snapshot->snake_count = 0;
    snapshot->food_count = 0;
    snapshot->hazard_count = 0;
    state->undo_count--;
    return true;
}

const Board* CoreSearchStateBoard(const CoreSearchState* state) {
    return state == NULL ? NULL : &state->board;
}
```

This first version preserves correctness by using snapshots internally. After the equivalence test is green, replace the internals of `CoreSearchStateMakeMoves()` with direct mutation of `state->board.snakes`, `state->board.food`, and `state->board.hazards`; keep the API and tests unchanged. The direct mutation version must store only changed snake bodies, health, lengths, eaten food coordinates, and eliminated snake records in the undo stack.

- [ ] **Step 3: Add source and route search through state when enabled**

Add to `setup.py`:

```python
"battlesnake/c-core/core/search_state.c",
```

In `core_algorithms.c`, include:

```c
#include "search_state.h"
```

In recursive search, replace clone branch:

```c
if (context->config.enable_make_unmake) {
    if (!CoreSearchStateMakeMoves(context->state, ids, moves, snake_count)) {
        return CORE_ERROR;
    }
    const Board* next = CoreSearchStateBoard(context->state);
    CoreStatus status = core_minimax_search(next, snake_id, depth - 1, alpha, child_beta, MOVE_INVALID, context, timed_out, &score, NULL);
    if (!CoreSearchStateUnmake(context->state)) {
        return CORE_ERROR;
    }
    if (status != CORE_OK || *timed_out) {
        return status;
    }
} else {
    context->stats->clone_calls++;
    context->stats->board_allocations++;
    Board* next = BoardCloneAndApply(board, ids, moves, snake_count);
    if (next == NULL) {
        return CORE_ERROR;
    }
    CoreStatus status = core_minimax_search(next, snake_id, depth - 1, alpha, child_beta, MOVE_INVALID, context, timed_out, &score, NULL);
    BoardFree(next);
    if (status != CORE_OK || *timed_out) {
        return status;
    }
}
```

Add to `CoreSearchContext`:

```c
CoreSearchState* state;
```

Initialize `CoreSearchState state;` in `CoreMinimaxMoveWithStats()` and set `context.state = &state` when `config.enable_make_unmake` is true.

- [ ] **Step 4: Replace snapshot internals with true make/unmake**

After tests pass with snapshot-backed state, rewrite `CoreSearchStateMakeMoves()` to:

1. Save current health, length, body length, and body coordinates for each snake that changes.
2. Save current food array and food count only when any snake eats.
3. Apply simultaneous moves into existing snake body buffers, growing buffers with `realloc()` only when length exceeds capacity.
4. Resolve out-of-bounds, health, body collision, and head-to-head collision in the same order as `BoardCloneAndApply()`.
5. Compact eliminated snakes in-place and record enough undo data to restore original snake order.
6. `CoreSearchStateUnmake()` restores the previous arrays and counts without calling `BoardCloneAndApply()`.

Keep `tests.test_search_diagnostics.SearchDiagnosticsTests.test_make_unmake_matches_clone_search_result` green after each sub-step.

- [ ] **Step 5: Rebuild, test, and benchmark**

Run:

```bash
python3 setup.py build_ext --inplace --force
python3 -m unittest discover -s tests -p 'test_*.py' -v
python3 -m benchmarks.run_minimax_bench --runs 20 --warmup 3 --budgets 180,320,450 --fixed-depths 0,4,6 --out benchmarks/results/after-makeunmake.jsonl
python3 -m benchmarks.compare_benchmarks benchmarks/results/after-workspace.jsonl benchmarks/results/after-makeunmake.jsonl
```

Expected: tests pass; fixed-depth rows show lower `clone_calls_p50` and lower `board_allocations_p50`; budget rows should complete equal or greater depth in at least the dense duel scenarios.

- [ ] **Step 6: Commit**

```bash
git add setup.py battlesnake/c-core/core/core_algorithms.c battlesnake/c-core/core/search_stats.h battlesnake/c-core/core/search_state.h battlesnake/c-core/core/search_state.c tests/test_search_diagnostics.py
git commit -m "perf: use in-place minimax make-unmake"
```

---

### Task 9: Final Benchmark Report And Guardrails

**Files:**
- Create: `benchmarks/results/README.md`
- Modify: `docs/superpowers/plans/2026-07-02-search-performance-optimization.md`

- [ ] **Step 1: Create benchmark result documentation**

Create `benchmarks/results/README.md`:

```markdown
# Benchmark Results

Generated `*.jsonl`, `*.json`, and `*.csv` files in this directory are ignored by git.

For optimization work, keep these local files while comparing:

- `baseline-before-tt.jsonl`
- `after-tt.jsonl`
- `after-ordering.jsonl`
- `after-workspace.jsonl`
- `after-makeunmake.jsonl`

Run:

```bash
python3 -m benchmarks.compare_benchmarks benchmarks/results/baseline-before-tt.jsonl benchmarks/results/after-makeunmake.jsonl
```
```

- [ ] **Step 2: Run full verification**

Run:

```bash
python3 setup.py build_ext --inplace --force
python3 -m unittest discover -s tests -p 'test_*.py' -v
python3 -m benchmarks.run_minimax_bench --runs 30 --warmup 5 --budgets 180,320,450 --fixed-depths 0,4,6,8 --out benchmarks/results/final.jsonl
python3 -m benchmarks.compare_benchmarks benchmarks/results/baseline-before-tt.jsonl benchmarks/results/final.jsonl
```

Expected: unit tests pass; comparator exits 0; final JSONL includes all scenarios and budgets.

- [ ] **Step 3: Record final local summary**

Append a short summary section to this plan after execution:

```markdown
## Execution Summary

- Baseline file: `benchmarks/results/baseline-before-tt.jsonl`
- Final file: `benchmarks/results/final.jsonl`
- Biggest depth gain:
- Biggest nodes/sec gain:
- Scenario with weakest gain:
- Residual risk:
```

Fill the four value lines with actual numbers from `final.jsonl` and `compare_benchmarks.py` output.

- [ ] **Step 4: Commit**

```bash
git add benchmarks/results/README.md docs/superpowers/plans/2026-07-02-search-performance-optimization.md
git commit -m "docs: record minimax optimization benchmark workflow"
```

---

## Self-Review

Spec coverage:

- Instrumentation and benchmarks are covered by Tasks 1-3 and receive the most detail. The suite includes eight realistic deterministic scenarios, budget mode, fixed-depth mode, counters, summaries, and comparison gates.
- Transposition table plus Zobrist hashing is covered by Tasks 4-5.
- Move ordering is covered by Task 6 with TT best move, previous iteration best move, principal variation, killer moves, and history scores.
- Allocation churn and make/unmake are covered by Tasks 7-8 with workspace reuse first and in-place search state second.

Placeholder scan:

- No placeholder markers.
- No empty test requests without concrete test code.
- No unnamed files or unspecified commands.

Type consistency:

- `CoreSearchStats`, `CoreSearchConfig`, `CoreTranspositionTable`, `CoreSearchWorkspace`, and `CoreSearchState` names are consistent across C headers, C implementation steps, wrappers, and tests.
- Python diagnostics API uses `minimax_diagnostics()` consistently in tests and benchmark scripts.
