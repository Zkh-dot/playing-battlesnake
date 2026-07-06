# C-Core LightGBM Opponent Model Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a minimal-latency production path that lets `c-core` score the trained Standard FFA LightGBM opponent-move model without Python in request handling.

**Architecture:** Export the selected LightGBM model into a repo-owned text artifact, convert that artifact into static C arrays, and evaluate the trees directly from `c-core`. Runtime code computes the same feature vector as offline training, scores four candidate moves per opponent snake, normalizes them into a move prior, and exposes the result through both the CPython native extension for parity tests and the native HTTP server path for future Standard FFA integration.

**Tech Stack:** C23, existing `battlesnake/c-core` board primitives, Python stdlib codegen tooling, LightGBM text model export for offline conversion only, `unittest`, native extension build via `python3 setup.py build_ext --inplace --force`, native server build via `tools/build_native_server.sh`.

---

## Scope

Included:

- publish a deterministic LightGBM text model export from `gbdt_lightgbm.joblib.gz`;
- generate static C model arrays from the LightGBM text export;
- implement no-allocation C inference for one candidate row and four candidate moves;
- implement C runtime feature extraction matching the offline feature list;
- add Python-extension wrappers only for tests and benchmarks;
- add parity tests comparing Python training features/scores to C features/scores;
- add native server build integration so the generated model compiles into production C binaries.

Excluded:

- changing `StrategyStandard.decide` behavior;
- building a full Standard FFA planner;
- invoking Python, LightGBM, joblib, or pandas from production request handling;
- adding dynamic model loading in production;
- adding runtime support for non-Standard rulesets.

## File Structure

- Create `tools/export_lightgbm_opponent_model.py`: decompresses `gbdt_lightgbm.joblib.gz`, loads the LightGBM model offline, and writes a deterministic LightGBM text model plus JSON metadata.
- Create `tools/generate_c_opponent_model.py`: parses the LightGBM text model and generates static C arrays.
- Create `tests/training/test_opponent_model_codegen.py`: tests the exporter/codegen on a small hand-written LightGBM-like model dump and verifies generated files.
- Create `battlesnake/c-core/core/opponent_model.h`: public C API for feature extraction, tree scoring, and score normalization.
- Create `battlesnake/c-core/core/opponent_model.c`: runtime-safe feature extraction and inference helpers.
- Create `battlesnake/c-core/core/opponent_model_generated.h`: generated constants, node arrays, and model metadata.
- Create `battlesnake/c-core/core/opponent_model_generated.c`: generated model arrays and feature-name metadata.
- Modify `battlesnake/c-core/py-core/py_core.c`: test-only CPython wrappers for `opponent_model_features`, `opponent_model_scores`, and `opponent_model_probabilities`.
- Modify `setup.py`: include `opponent_model.c` and `opponent_model_generated.c` in the Python native extension build.
- Modify `tools/build_native_server.sh`: include `opponent_model.c` and `opponent_model_generated.c` in the native server build.
- Create `tests/test_native_opponent_model.py`: Python-facing parity and API tests.
- Create `tests/c/test_opponent_model.c`: C-only unit tests for sigmoid, normalization, tree traversal, and move ordering.
- Modify or create `tests/c/build_test_opponent_model.sh`: compile the C-only tests with the same source files as production.
- Create `tools/benchmark_native_opponent_model.py`: measures C inference latency through the native extension on representative Standard FFA boards.
- Modify `docs/opponent-model-runtime-report.md`: record the C export artifact, parity checks, and measured C inference latency.

## Data Contract

Runtime model input feature order must exactly match the offline training `FEATURE_COLUMNS` after `snake_name` was removed:

```text
0  feature_candidate_move
1  feature_turn
2  board_width
3  board_height
4  alive_snakes
5  feature_snake_rank
6  snake_health
7  snake_length
8  safe_moves_count
9  candidate_is_safe
10 candidate_in_bounds
11 candidate_occupied_without_tails
12 candidate_is_food
13 candidate_is_hazard
14 candidate_to_nearest_food
15 head_to_nearest_food
16 candidate_center_distance
17 candidate_reachable_space
18 adjacent_longer_or_equal_heads
19 adjacent_shorter_heads
```

`feature_candidate_move` is encoded as:

```text
up=0, down=1, left=2, right=3
```

The C API returns raw LightGBM binary-class scores after sigmoid. Callers that need a distribution over the four candidate moves must normalize the four non-negative scores so they sum to `1.0`. If all four scores are zero or non-finite, return a uniform distribution.

`feature_candidate_move` was trained as a categorical feature. The generated C model must therefore support both numerical `<=` splits and categorical membership splits. The converter must fail with a non-zero exit code if it sees an unknown LightGBM `decision_type`, missing categorical boundary data, or a categorical threshold it cannot decode exactly. Silent conversion to a numerical threshold is not acceptable for production.

---

### Task 1: Export the Published Model to a Deterministic LightGBM Text Artifact

**Files:**
- Create: `tools/export_lightgbm_opponent_model.py`
- Create: `tests/training/test_opponent_model_codegen.py`
- Generate: `ai-artifacts/opponent-model/gbdt_lightgbm.txt`
- Generate: `ai-artifacts/opponent-model/gbdt_lightgbm_export.json`

- [ ] **Step 1: Write the failing exporter test**

Add this file:

```python
# tests/training/test_opponent_model_codegen.py
from __future__ import annotations

import gzip
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import Mock, patch


class OpponentModelExportTests(unittest.TestCase):
    def test_exporter_writes_lightgbm_text_and_metadata(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            archive = root / "model.joblib.gz"
            output = root / "model.txt"
            metadata = root / "model.json"
            with gzip.open(archive, "wb") as handle:
                handle.write(b"joblib-bytes")

            booster = Mock()
            booster.current_iteration.return_value = 17
            booster.num_model_per_iteration.return_value = 1
            model = Mock()
            model.booster_ = booster

            with patch("joblib.load", return_value=model), patch.object(booster, "save_model") as save_model:
                result = subprocess.run(
                    [
                        sys.executable,
                        "tools/export_lightgbm_opponent_model.py",
                        "--model-archive",
                        str(archive),
                        "--output",
                        str(output),
                        "--metadata",
                        str(metadata),
                    ],
                    check=True,
                    text=True,
                    capture_output=True,
                )

        self.assertIn("model_text", result.stdout)
        save_model.assert_called_once_with(str(output), num_iteration=17)
        payload = json.loads(metadata.read_text())
        self.assertEqual(payload["model_archive"], str(archive))
        self.assertEqual(payload["model_text"], str(output))
        self.assertEqual(payload["best_iteration"], 17)
        self.assertEqual(payload["features"][0], "feature_candidate_move")
        self.assertEqual(payload["move_encoding"], {"up": 0, "down": 1, "left": 2, "right": 3})


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the test and verify it fails**

Run:

```bash
PYTHONPATH=.venv-ml/lib/python3.10/site-packages python3 -m unittest tests.training.test_opponent_model_codegen -v
```

Expected: FAIL with `No such file or directory` or `ModuleNotFoundError` for `tools/export_lightgbm_opponent_model.py`.

- [ ] **Step 3: Implement the exporter**

Create `tools/export_lightgbm_opponent_model.py`:

```python
#!/usr/bin/env python3
from __future__ import annotations

import argparse
import gzip
import json
import shutil
import sys
import tempfile
from pathlib import Path

import joblib

FEATURE_COLUMNS = [
    "feature_candidate_move",
    "feature_turn",
    "board_width",
    "board_height",
    "alive_snakes",
    "feature_snake_rank",
    "snake_health",
    "snake_length",
    "safe_moves_count",
    "candidate_is_safe",
    "candidate_in_bounds",
    "candidate_occupied_without_tails",
    "candidate_is_food",
    "candidate_is_hazard",
    "candidate_to_nearest_food",
    "head_to_nearest_food",
    "candidate_center_distance",
    "candidate_reachable_space",
    "adjacent_longer_or_equal_heads",
    "adjacent_shorter_heads",
]
MOVE_ENCODING = {"up": 0, "down": 1, "left": 2, "right": 3}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Export the selected opponent LightGBM model.")
    parser.add_argument("--model-archive", type=Path, default=Path("ai-artifacts/opponent-model/gbdt_lightgbm.joblib.gz"))
    parser.add_argument("--output", type=Path, default=Path("ai-artifacts/opponent-model/gbdt_lightgbm.txt"))
    parser.add_argument("--metadata", type=Path, default=Path("ai-artifacts/opponent-model/gbdt_lightgbm_export.json"))
    return parser.parse_args()


def load_model_from_gzip(path: Path):
    with tempfile.NamedTemporaryFile(suffix=".joblib") as handle:
        with gzip.open(path, "rb") as source:
            shutil.copyfileobj(source, handle)
        handle.flush()
        return joblib.load(handle.name)


def main() -> int:
    args = parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.metadata.parent.mkdir(parents=True, exist_ok=True)
    model = load_model_from_gzip(args.model_archive)
    booster = model.booster_
    best_iteration = int(booster.current_iteration())
    booster.save_model(str(args.output), num_iteration=best_iteration)
    payload = {
        "model_archive": str(args.model_archive),
        "model_text": str(args.output),
        "best_iteration": best_iteration,
        "num_model_per_iteration": int(booster.num_model_per_iteration()),
        "features": FEATURE_COLUMNS,
        "move_encoding": MOVE_ENCODING,
        "runtime": "c-core-static-arrays",
    }
    args.metadata.write_text(json.dumps(payload, indent=2, sort_keys=True))
    print(json.dumps({"model_text": str(args.output), "metadata": str(args.metadata)}, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

- [ ] **Step 4: Make the exporter executable**

Run:

```bash
chmod +x tools/export_lightgbm_opponent_model.py
```

Expected: command exits with code `0`.

- [ ] **Step 5: Run the exporter test and verify it passes**

Run:

```bash
PYTHONPATH=.venv-ml/lib/python3.10/site-packages python3 -m unittest tests.training.test_opponent_model_codegen -v
```

Expected: PASS.

- [ ] **Step 6: Export the real model**

Run:

```bash
PYTHONPATH=.venv-ml/lib/python3.10/site-packages python3 tools/export_lightgbm_opponent_model.py \
  --model-archive ai-artifacts/opponent-model/gbdt_lightgbm.joblib.gz \
  --output ai-artifacts/opponent-model/gbdt_lightgbm.txt \
  --metadata ai-artifacts/opponent-model/gbdt_lightgbm_export.json
```

Expected:

- stdout contains `gbdt_lightgbm.txt`;
- `ai-artifacts/opponent-model/gbdt_lightgbm.txt` exists;
- `ai-artifacts/opponent-model/gbdt_lightgbm_export.json` exists;
- metadata feature list starts with `feature_candidate_move`.

- [ ] **Step 7: Commit**

Run:

```bash
git add tools/export_lightgbm_opponent_model.py tests/training/test_opponent_model_codegen.py
git commit -m "tools: export opponent lightgbm model"
```

---

### Task 2: Add a Small C Inference Kernel With a Hand-Written Model

**Files:**
- Create: `battlesnake/c-core/core/opponent_model.h`
- Create: `battlesnake/c-core/core/opponent_model.c`
- Create: `battlesnake/c-core/core/opponent_model_generated.h`
- Create: `battlesnake/c-core/core/opponent_model_generated.c`
- Create: `tests/c/test_opponent_model.c`
- Create or modify: `tests/c/build_test_opponent_model.sh`

- [ ] **Step 1: Write the C test**

Create `tests/c/test_opponent_model.c`:

```c
#include "../../battlesnake/c-core/core/opponent_model.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static void test_sigmoid_bounds(void) {
    assert(CoreOpponentModelSigmoid(0.0) > 0.499);
    assert(CoreOpponentModelSigmoid(0.0) < 0.501);
    assert(CoreOpponentModelSigmoid(10.0) > 0.999);
    assert(CoreOpponentModelSigmoid(-10.0) < 0.001);
}

static void test_normalize_scores(void) {
    double scores[4] = {2.0, 1.0, 1.0, 0.0};
    double probabilities[4] = {0.0, 0.0, 0.0, 0.0};
    CoreOpponentModelNormalizeScores(scores, probabilities);
    assert(fabs(probabilities[MOVE_UP] - 0.5) < 1e-12);
    assert(fabs(probabilities[MOVE_DOWN] - 0.25) < 1e-12);
    assert(fabs(probabilities[MOVE_LEFT] - 0.25) < 1e-12);
    assert(fabs(probabilities[MOVE_RIGHT] - 0.0) < 1e-12);
}

static void test_normalize_uniform_for_invalid_sum(void) {
    double scores[4] = {0.0, -1.0, 0.0, NAN};
    double probabilities[4] = {0.0, 0.0, 0.0, 0.0};
    CoreOpponentModelNormalizeScores(scores, probabilities);
    assert(fabs(probabilities[MOVE_UP] - 0.25) < 1e-12);
    assert(fabs(probabilities[MOVE_DOWN] - 0.25) < 1e-12);
    assert(fabs(probabilities[MOVE_LEFT] - 0.25) < 1e-12);
    assert(fabs(probabilities[MOVE_RIGHT] - 0.25) < 1e-12);
}

static void test_hand_written_tree_scores_candidate_move(void) {
    CoreOpponentFeatureVector features = {0};
    features.values[CORE_OPPONENT_FEATURE_CANDIDATE_MOVE] = (double)MOVE_RIGHT;
    features.values[CORE_OPPONENT_FEATURE_CANDIDATE_IS_SAFE] = 1.0;
    double right_score = CoreOpponentModelScoreFeatures(&features);
    features.values[CORE_OPPONENT_FEATURE_CANDIDATE_MOVE] = (double)MOVE_LEFT;
    double left_score = CoreOpponentModelScoreFeatures(&features);
    assert(right_score > left_score);
}

int main(void) {
    test_sigmoid_bounds();
    test_normalize_scores();
    test_normalize_uniform_for_invalid_sum();
    test_hand_written_tree_scores_candidate_move();
    puts("test_opponent_model OK");
    return 0;
}
```

- [ ] **Step 2: Add the C test build script**

Create `tests/c/build_test_opponent_model.sh`:

```bash
#!/usr/bin/env bash
set -euo pipefail

mkdir -p build/tests
gcc \
  -std=c2x \
  -D_POSIX_C_SOURCE=200809L \
  -O2 \
  -Ibattlesnake/c-core \
  tests/c/test_opponent_model.c \
  battlesnake/c-core/datatypes/coord.c \
  battlesnake/c-core/datatypes/snake.c \
  battlesnake/c-core/datatypes/board.c \
  battlesnake/c-core/core/core_algorithms.c \
  battlesnake/c-core/core/position_eval.c \
  battlesnake/c-core/core/search_stats.c \
  battlesnake/c-core/core/search_workspace.c \
  battlesnake/c-core/core/search_state.c \
  battlesnake/c-core/core/zobrist.c \
  battlesnake/c-core/core/transposition_table.c \
  battlesnake/c-core/core/opponent_model.c \
  battlesnake/c-core/core/opponent_model_generated.c \
  -lm \
  -o build/tests/test_opponent_model
```

- [ ] **Step 3: Run the C test and verify it fails**

Run:

```bash
chmod +x tests/c/build_test_opponent_model.sh
tests/c/build_test_opponent_model.sh
```

Expected: FAIL because `battlesnake/c-core/core/opponent_model.h` does not exist.

- [ ] **Step 4: Add the public C API**

Create `battlesnake/c-core/core/opponent_model.h`:

```c
#pragma once

#include "../datatypes/board.h"

#include <stdbool.h>

typedef enum {
    CORE_OPPONENT_FEATURE_CANDIDATE_MOVE = 0,
    CORE_OPPONENT_FEATURE_TURN = 1,
    CORE_OPPONENT_FEATURE_BOARD_WIDTH = 2,
    CORE_OPPONENT_FEATURE_BOARD_HEIGHT = 3,
    CORE_OPPONENT_FEATURE_ALIVE_SNAKES = 4,
    CORE_OPPONENT_FEATURE_SNAKE_RANK = 5,
    CORE_OPPONENT_FEATURE_SNAKE_HEALTH = 6,
    CORE_OPPONENT_FEATURE_SNAKE_LENGTH = 7,
    CORE_OPPONENT_FEATURE_SAFE_MOVES_COUNT = 8,
    CORE_OPPONENT_FEATURE_CANDIDATE_IS_SAFE = 9,
    CORE_OPPONENT_FEATURE_CANDIDATE_IN_BOUNDS = 10,
    CORE_OPPONENT_FEATURE_CANDIDATE_OCCUPIED_WITHOUT_TAILS = 11,
    CORE_OPPONENT_FEATURE_CANDIDATE_IS_FOOD = 12,
    CORE_OPPONENT_FEATURE_CANDIDATE_IS_HAZARD = 13,
    CORE_OPPONENT_FEATURE_CANDIDATE_TO_NEAREST_FOOD = 14,
    CORE_OPPONENT_FEATURE_HEAD_TO_NEAREST_FOOD = 15,
    CORE_OPPONENT_FEATURE_CANDIDATE_CENTER_DISTANCE = 16,
    CORE_OPPONENT_FEATURE_CANDIDATE_REACHABLE_SPACE = 17,
    CORE_OPPONENT_FEATURE_ADJACENT_LONGER_OR_EQUAL_HEADS = 18,
    CORE_OPPONENT_FEATURE_ADJACENT_SHORTER_HEADS = 19,
    CORE_OPPONENT_FEATURE_COUNT = 20,
} CoreOpponentFeatureIndex;

typedef struct {
    double values[CORE_OPPONENT_FEATURE_COUNT];
} CoreOpponentFeatureVector;

typedef enum {
    CORE_OPPONENT_SPLIT_NUMERIC_LE = 0,
    CORE_OPPONENT_SPLIT_CATEGORICAL_IN = 1,
} CoreOpponentSplitType;

typedef struct {
    int feature_index;
    double threshold;
    int left_child;
    int right_child;
    double leaf_value;
    bool is_leaf;
    CoreOpponentSplitType split_type;
    int category_offset;
    int category_count;
} CoreOpponentTreeNode;

typedef struct {
    const CoreOpponentTreeNode* nodes;
    int node_count;
} CoreOpponentTree;

double CoreOpponentModelSigmoid(double value);
double CoreOpponentModelScoreFeatures(const CoreOpponentFeatureVector* features);
void CoreOpponentModelNormalizeScores(const double scores[4], double probabilities[4]);
CoreStatus CoreOpponentModelFeatures(
    const Board* board,
    const char* snake_id,
    int turn,
    int snake_rank,
    MoveDirection candidate_move,
    CoreOpponentFeatureVector* out_features
);
CoreStatus CoreOpponentModelScores(
    const Board* board,
    const char* snake_id,
    int turn,
    int snake_rank,
    double out_scores[4]
);
CoreStatus CoreOpponentModelProbabilities(
    const Board* board,
    const char* snake_id,
    int turn,
    int snake_rank,
    double out_probabilities[4]
);
```

- [ ] **Step 5: Add the initial hand-written generated model**

Create `battlesnake/c-core/core/opponent_model_generated.h`:

```c
#pragma once

#include "opponent_model.h"

extern const CoreOpponentTree CoreOpponentModelTrees[];
extern const int CoreOpponentModelTreeCount;
extern const double CoreOpponentModelInitialScore;
extern const int CoreOpponentModelCategoryValues[];
extern const int CoreOpponentModelCategoryValueCount;
extern const char* const CoreOpponentModelFeatureNames[CORE_OPPONENT_FEATURE_COUNT];
```

Create `battlesnake/c-core/core/opponent_model_generated.c`:

```c
#include "opponent_model_generated.h"

static const CoreOpponentTreeNode TREE_0_NODES[] = {
    {CORE_OPPONENT_FEATURE_CANDIDATE_MOVE, 2.5, 1, 2, 0.0, false, CORE_OPPONENT_SPLIT_NUMERIC_LE, 0, 0},
    {-1, 0.0, -1, -1, -1.0, true, CORE_OPPONENT_SPLIT_NUMERIC_LE, 0, 0},
    {-1, 0.0, -1, -1, 1.0, true, CORE_OPPONENT_SPLIT_NUMERIC_LE, 0, 0},
};

const CoreOpponentTree CoreOpponentModelTrees[] = {
    {TREE_0_NODES, 3},
};

const int CoreOpponentModelTreeCount = 1;
const double CoreOpponentModelInitialScore = 0.0;
const int CoreOpponentModelCategoryValues[] = {0};
const int CoreOpponentModelCategoryValueCount = 1;

const char* const CoreOpponentModelFeatureNames[CORE_OPPONENT_FEATURE_COUNT] = {
    "feature_candidate_move",
    "feature_turn",
    "board_width",
    "board_height",
    "alive_snakes",
    "feature_snake_rank",
    "snake_health",
    "snake_length",
    "safe_moves_count",
    "candidate_is_safe",
    "candidate_in_bounds",
    "candidate_occupied_without_tails",
    "candidate_is_food",
    "candidate_is_hazard",
    "candidate_to_nearest_food",
    "head_to_nearest_food",
    "candidate_center_distance",
    "candidate_reachable_space",
    "adjacent_longer_or_equal_heads",
    "adjacent_shorter_heads",
};
```

- [ ] **Step 6: Implement the minimal inference kernel**

Create `battlesnake/c-core/core/opponent_model.c`:

```c
#include "opponent_model.h"
#include "opponent_model_generated.h"

#include <math.h>

double CoreOpponentModelSigmoid(double value) {
    if (value >= 40.0) {
        return 1.0;
    }
    if (value <= -40.0) {
        return 0.0;
    }
    return 1.0 / (1.0 + exp(-value));
}

static double score_tree(const CoreOpponentTree* tree, const CoreOpponentFeatureVector* features) {
    int index = 0;
    while (index >= 0 && index < tree->node_count) {
        const CoreOpponentTreeNode* node = &tree->nodes[index];
        if (node->is_leaf) {
            return node->leaf_value;
        }
        double value = features->values[node->feature_index];
        bool go_left = false;
        if (node->split_type == CORE_OPPONENT_SPLIT_CATEGORICAL_IN) {
            int category = (int)value;
            for (int i = 0; i < node->category_count; ++i) {
                if (CoreOpponentModelCategoryValues[node->category_offset + i] == category) {
                    go_left = true;
                    break;
                }
            }
        } else {
            go_left = value <= node->threshold;
        }
        index = go_left ? node->left_child : node->right_child;
    }
    return 0.0;
}

double CoreOpponentModelScoreFeatures(const CoreOpponentFeatureVector* features) {
    double raw_score = CoreOpponentModelInitialScore;
    for (int i = 0; i < CoreOpponentModelTreeCount; ++i) {
        raw_score += score_tree(&CoreOpponentModelTrees[i], features);
    }
    return CoreOpponentModelSigmoid(raw_score);
}

void CoreOpponentModelNormalizeScores(const double scores[4], double probabilities[4]) {
    double sum = 0.0;
    for (int i = 0; i < 4; ++i) {
        if (isfinite(scores[i]) && scores[i] > 0.0) {
            sum += scores[i];
        }
    }
    if (!(sum > 0.0)) {
        for (int i = 0; i < 4; ++i) {
            probabilities[i] = 0.25;
        }
        return;
    }
    for (int i = 0; i < 4; ++i) {
        probabilities[i] = (isfinite(scores[i]) && scores[i] > 0.0) ? scores[i] / sum : 0.0;
    }
}

CoreStatus CoreOpponentModelFeatures(
    const Board* board,
    const char* snake_id,
    int turn,
    int snake_rank,
    MoveDirection candidate_move,
    CoreOpponentFeatureVector* out_features
) {
    (void)board;
    (void)snake_id;
    for (int i = 0; i < CORE_OPPONENT_FEATURE_COUNT; ++i) {
        out_features->values[i] = 0.0;
    }
    out_features->values[CORE_OPPONENT_FEATURE_CANDIDATE_MOVE] = (double)candidate_move;
    out_features->values[CORE_OPPONENT_FEATURE_TURN] = (double)turn;
    out_features->values[CORE_OPPONENT_FEATURE_SNAKE_RANK] = snake_rank < 0 ? 999.0 : (double)snake_rank;
    return CORE_OK;
}

CoreStatus CoreOpponentModelScores(
    const Board* board,
    const char* snake_id,
    int turn,
    int snake_rank,
    double out_scores[4]
) {
    for (int move = MOVE_UP; move <= MOVE_RIGHT; ++move) {
        CoreOpponentFeatureVector features;
        CoreStatus status = CoreOpponentModelFeatures(
            board,
            snake_id,
            turn,
            snake_rank,
            (MoveDirection)move,
            &features
        );
        if (status != CORE_OK) {
            return status;
        }
        out_scores[move] = CoreOpponentModelScoreFeatures(&features);
    }
    return CORE_OK;
}

CoreStatus CoreOpponentModelProbabilities(
    const Board* board,
    const char* snake_id,
    int turn,
    int snake_rank,
    double out_probabilities[4]
) {
    double scores[4];
    CoreStatus status = CoreOpponentModelScores(board, snake_id, turn, snake_rank, scores);
    if (status != CORE_OK) {
        return status;
    }
    CoreOpponentModelNormalizeScores(scores, out_probabilities);
    return CORE_OK;
}
```

- [ ] **Step 7: Run the C test and verify it passes**

Run:

```bash
tests/c/build_test_opponent_model.sh
./build/tests/test_opponent_model
```

Expected output:

```text
test_opponent_model OK
```

- [ ] **Step 8: Commit**

Run:

```bash
git add battlesnake/c-core/core/opponent_model.h \
  battlesnake/c-core/core/opponent_model.c \
  battlesnake/c-core/core/opponent_model_generated.h \
  battlesnake/c-core/core/opponent_model_generated.c \
  tests/c/test_opponent_model.c \
  tests/c/build_test_opponent_model.sh
git commit -m "feat: add c opponent model inference kernel"
```

---

### Task 3: Generate Static C Arrays From the LightGBM Text Model

**Files:**
- Modify: `tools/generate_c_opponent_model.py`
- Modify: `tests/training/test_opponent_model_codegen.py`
- Generate: `battlesnake/c-core/core/opponent_model_generated.h`
- Generate: `battlesnake/c-core/core/opponent_model_generated.c`

- [ ] **Step 1: Add a codegen parser test**

Append this test class to `tests/training/test_opponent_model_codegen.py`:

```python
class OpponentModelCodegenTests(unittest.TestCase):
    def test_codegen_writes_static_c_arrays_for_small_text_model(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            model_text = root / "small_model.txt"
            header = root / "opponent_model_generated.h"
            source = root / "opponent_model_generated.c"
            model_text.write_text(
                "\n".join(
                    [
                        "tree",
                        "version=v4",
                        "num_class=1",
                        "tree_sizes=1",
                        "",
                        "Tree=0",
                        "num_leaves=2",
                        "num_cat=0",
                        "split_feature=0",
                        "threshold=2.5",
                        "decision_type=2",
                        "left_child=-1",
                        "right_child=-2",
                        "leaf_value=-1.0 1.0",
                        "",
                        "end of trees",
                        "feature_names=feature_candidate_move feature_turn board_width board_height alive_snakes feature_snake_rank snake_health snake_length safe_moves_count candidate_is_safe candidate_in_bounds candidate_occupied_without_tails candidate_is_food candidate_is_hazard candidate_to_nearest_food head_to_nearest_food candidate_center_distance candidate_reachable_space adjacent_longer_or_equal_heads adjacent_shorter_heads",
                        "average_output=false",
                        "sigmoid=1",
                    ]
                )
            )

            result = subprocess.run(
                [
                    sys.executable,
                    "tools/generate_c_opponent_model.py",
                    "--model-text",
                    str(model_text),
                    "--header",
                    str(header),
                    "--source",
                    str(source),
                ],
                check=True,
                text=True,
                capture_output=True,
            )

        self.assertIn("opponent_model_generated.c", result.stdout)
        self.assertIn("CoreOpponentModelTrees", source.read_text())
        self.assertIn("TREE_0_NODES", source.read_text())
        self.assertIn("feature_candidate_move", source.read_text())
        self.assertIn("CoreOpponentModelTreeCount = 1", source.read_text())
        self.assertIn("extern const CoreOpponentTree CoreOpponentModelTrees[]", header.read_text())

    def test_codegen_preserves_categorical_membership_splits(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            model_text = root / "categorical_model.txt"
            header = root / "opponent_model_generated.h"
            source = root / "opponent_model_generated.c"
            model_text.write_text(
                "\n".join(
                    [
                        "tree",
                        "version=v4",
                        "num_class=1",
                        "tree_sizes=1",
                        "",
                        "Tree=0",
                        "num_leaves=2",
                        "num_cat=1",
                        "split_feature=0",
                        "threshold=0",
                        "decision_type=9",
                        "left_child=-1",
                        "right_child=-2",
                        "leaf_value=0.75 -0.25",
                        "cat_boundaries=0 2",
                        "cat_threshold=0 2",
                        "",
                        "end of trees",
                        "feature_names=feature_candidate_move feature_turn board_width board_height alive_snakes feature_snake_rank snake_health snake_length safe_moves_count candidate_is_safe candidate_in_bounds candidate_occupied_without_tails candidate_is_food candidate_is_hazard candidate_to_nearest_food head_to_nearest_food candidate_center_distance candidate_reachable_space adjacent_longer_or_equal_heads adjacent_shorter_heads",
                        "average_output=false",
                        "sigmoid=1",
                    ]
                )
            )

            subprocess.run(
                [
                    sys.executable,
                    "tools/generate_c_opponent_model.py",
                    "--model-text",
                    str(model_text),
                    "--header",
                    str(header),
                    "--source",
                    str(source),
                ],
                check=True,
                text=True,
                capture_output=True,
            )

        generated = source.read_text()
        self.assertIn("CORE_OPPONENT_SPLIT_CATEGORICAL_IN", generated)
        self.assertIn("CoreOpponentModelCategoryValues[] = {0, 2}", generated)
```

- [ ] **Step 2: Run the test and verify it fails**

Run:

```bash
PYTHONPATH=.venv-ml/lib/python3.10/site-packages python3 -m unittest tests.training.test_opponent_model_codegen -v
```

Expected: FAIL because `tools/generate_c_opponent_model.py` does not exist.

- [ ] **Step 3: Implement the converter**

Create `tools/generate_c_opponent_model.py`:

```python
#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from dataclasses import dataclass
from pathlib import Path

FEATURE_NAMES = [
    "feature_candidate_move",
    "feature_turn",
    "board_width",
    "board_height",
    "alive_snakes",
    "feature_snake_rank",
    "snake_health",
    "snake_length",
    "safe_moves_count",
    "candidate_is_safe",
    "candidate_in_bounds",
    "candidate_occupied_without_tails",
    "candidate_is_food",
    "candidate_is_hazard",
    "candidate_to_nearest_food",
    "head_to_nearest_food",
    "candidate_center_distance",
    "candidate_reachable_space",
    "adjacent_longer_or_equal_heads",
    "adjacent_shorter_heads",
]
FEATURE_INDEX = {name: index for index, name in enumerate(FEATURE_NAMES)}


@dataclass(frozen=True)
class Node:
    feature_index: int
    threshold: float
    left_child: int
    right_child: int
    leaf_value: float
    is_leaf: bool
    split_type: str
    categories: tuple[int, ...]


@dataclass(frozen=True)
class Tree:
    nodes: list[Node]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate C arrays for the opponent LightGBM model.")
    parser.add_argument("--model-text", type=Path, default=Path("ai-artifacts/opponent-model/gbdt_lightgbm.txt"))
    parser.add_argument("--header", type=Path, default=Path("battlesnake/c-core/core/opponent_model_generated.h"))
    parser.add_argument("--source", type=Path, default=Path("battlesnake/c-core/core/opponent_model_generated.c"))
    return parser.parse_args()


def parse_key_values(lines: list[str]) -> list[dict[str, str]]:
    trees: list[dict[str, str]] = []
    current: dict[str, str] | None = None
    for line in lines:
        line = line.strip()
        if not line:
            continue
        if line.startswith("Tree="):
            if current is not None:
                trees.append(current)
            current = {"Tree": line.split("=", 1)[1]}
            continue
        if line == "end of trees":
            break
        if current is not None and "=" in line:
            key, value = line.split("=", 1)
            current[key] = value
    if current is not None:
        trees.append(current)
    return trees


def parse_int_list(value: str) -> list[int]:
    return [int(item) for item in value.split()] if value else []


def parse_float_list(value: str) -> list[float]:
    return [float(item) for item in value.split()] if value else []


def convert_child(raw_child: int, split_count: int) -> int:
    if raw_child >= 0:
        return raw_child
    leaf_index = -raw_child - 1
    return split_count + leaf_index


def parse_tree(raw: dict[str, str]) -> Tree:
    split_features = parse_int_list(raw.get("split_feature", ""))
    thresholds = parse_float_list(raw.get("threshold", ""))
    decision_types = parse_int_list(raw.get("decision_type", ""))
    left_children = parse_int_list(raw.get("left_child", ""))
    right_children = parse_int_list(raw.get("right_child", ""))
    leaf_values = parse_float_list(raw.get("leaf_value", ""))
    cat_boundaries = parse_int_list(raw.get("cat_boundaries", ""))
    cat_threshold = parse_int_list(raw.get("cat_threshold", ""))
    split_count = len(split_features)
    nodes: list[Node] = []
    for index in range(split_count):
        decision_type = decision_types[index] if index < len(decision_types) else 2
        categorical = bool(decision_type & 1)
        categories: tuple[int, ...] = ()
        split_type = "CORE_OPPONENT_SPLIT_NUMERIC_LE"
        if categorical:
            category_set_index = int(thresholds[index])
            if category_set_index < 0 or category_set_index + 1 >= len(cat_boundaries):
                raise ValueError(f"invalid categorical threshold index {category_set_index} in tree {raw.get('Tree')}")
            begin = cat_boundaries[category_set_index]
            end = cat_boundaries[category_set_index + 1]
            if begin < 0 or end > len(cat_threshold) or begin > end:
                raise ValueError(f"invalid categorical boundary range {begin}:{end} in tree {raw.get('Tree')}")
            categories = tuple(cat_threshold[begin:end])
            split_type = "CORE_OPPONENT_SPLIT_CATEGORICAL_IN"
        nodes.append(
            Node(
                feature_index=split_features[index],
                threshold=thresholds[index],
                left_child=convert_child(left_children[index], split_count),
                right_child=convert_child(right_children[index], split_count),
                leaf_value=0.0,
                is_leaf=False,
                split_type=split_type,
                categories=categories,
            )
        )
    for value in leaf_values:
        nodes.append(Node(-1, 0.0, -1, -1, value, True, "CORE_OPPONENT_SPLIT_NUMERIC_LE", ()))
    return Tree(nodes)


def parse_model(path: Path) -> list[Tree]:
    raw_trees = parse_key_values(path.read_text().splitlines())
    return [parse_tree(raw) for raw in raw_trees]


def c_double(value: float) -> str:
    return f"{value:.17g}"


def write_header(path: Path) -> None:
    path.write_text(
        """#pragma once

#include "opponent_model.h"

extern const CoreOpponentTree CoreOpponentModelTrees[];
extern const int CoreOpponentModelTreeCount;
extern const double CoreOpponentModelInitialScore;
extern const int CoreOpponentModelCategoryValues[];
extern const int CoreOpponentModelCategoryValueCount;
extern const char* const CoreOpponentModelFeatureNames[CORE_OPPONENT_FEATURE_COUNT];
"""
    )


def write_source(path: Path, trees: list[Tree]) -> None:
    lines = ['#include "opponent_model_generated.h"', ""]
    category_values: list[int] = []
    for tree_index, tree in enumerate(trees):
        lines.append(f"static const CoreOpponentTreeNode TREE_{tree_index}_NODES[] = {{")
        for node in tree.nodes:
            is_leaf = "true" if node.is_leaf else "false"
            category_offset = len(category_values)
            category_values.extend(node.categories)
            lines.append(
                "    {"
                f"{node.feature_index}, {c_double(node.threshold)}, {node.left_child}, {node.right_child}, "
                f"{c_double(node.leaf_value)}, {is_leaf}, {node.split_type}, {category_offset}, {len(node.categories)}"
                "},"
            )
        lines.append("};")
        lines.append("")
    lines.append("const CoreOpponentTree CoreOpponentModelTrees[] = {")
    for tree_index, tree in enumerate(trees):
        lines.append(f"    {{TREE_{tree_index}_NODES, {len(tree.nodes)}}},")
    lines.append("};")
    lines.append("")
    lines.append(f"const int CoreOpponentModelTreeCount = {len(trees)};")
    lines.append("const double CoreOpponentModelInitialScore = 0.0;")
    category_literal = ", ".join(str(value) for value in category_values) or "0"
    lines.append(f"const int CoreOpponentModelCategoryValues[] = {{{category_literal}}};")
    lines.append(f"const int CoreOpponentModelCategoryValueCount = {len(category_values)};")
    lines.append("")
    lines.append("const char* const CoreOpponentModelFeatureNames[CORE_OPPONENT_FEATURE_COUNT] = {")
    for name in FEATURE_NAMES:
        lines.append(f'    "{name}",')
    lines.append("};")
    lines.append("")
    path.write_text("\n".join(lines))


def main() -> int:
    args = parse_args()
    trees = parse_model(args.model_text)
    args.header.parent.mkdir(parents=True, exist_ok=True)
    args.source.parent.mkdir(parents=True, exist_ok=True)
    write_header(args.header)
    write_source(args.source, trees)
    print(json.dumps({"header": str(args.header), "source": str(args.source), "trees": len(trees)}, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

- [ ] **Step 4: Make the converter executable**

Run:

```bash
chmod +x tools/generate_c_opponent_model.py
```

Expected: command exits with code `0`.

- [ ] **Step 5: Run the codegen tests**

Run:

```bash
PYTHONPATH=.venv-ml/lib/python3.10/site-packages python3 -m unittest tests.training.test_opponent_model_codegen -v
```

Expected: PASS.

- [ ] **Step 6: Generate the real C model arrays**

Run:

```bash
PYTHONPATH=.venv-ml/lib/python3.10/site-packages python3 tools/generate_c_opponent_model.py \
  --model-text ai-artifacts/opponent-model/gbdt_lightgbm.txt \
  --header battlesnake/c-core/core/opponent_model_generated.h \
  --source battlesnake/c-core/core/opponent_model_generated.c
```

Expected:

- stdout contains `"trees": 3000`;
- generated `.c` contains `TREE_0_NODES`;
- generated `.h` contains `CoreOpponentModelTrees`.

- [ ] **Step 7: Re-run C inference tests against the generated model**

Run:

```bash
tests/c/build_test_opponent_model.sh
./build/tests/test_opponent_model
```

Expected output:

```text
test_opponent_model OK
```

- [ ] **Step 8: Commit**

Run:

```bash
git add tools/generate_c_opponent_model.py \
  tests/training/test_opponent_model_codegen.py \
  battlesnake/c-core/core/opponent_model_generated.h \
  battlesnake/c-core/core/opponent_model_generated.c
git commit -m "tools: generate c opponent model arrays"
```

---

### Task 4: Implement Runtime Feature Extraction in C

**Files:**
- Modify: `battlesnake/c-core/core/opponent_model.c`
- Modify: `tests/c/test_opponent_model.c`

- [ ] **Step 1: Add C feature extraction assertions**

Replace `tests/c/test_opponent_model.c` with this expanded version:

```c
#include "../../battlesnake/c-core/core/opponent_model.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static Snake make_snake(const char* id, const char* name, int health, const Coord* body, int body_count) {
    Snake snake;
    memset(&snake, 0, sizeof(Snake));
    snake.id = strdup(id);
    snake.name = strdup(name);
    snake.health = health;
    snake.length = body_count;
    snake.body_count = body_count;
    snake.body = malloc(sizeof(Coord) * (size_t)body_count);
    for (int i = 0; i < body_count; ++i) {
        snake.body[i] = body[i];
    }
    return snake;
}

static Board* make_feature_board(void) {
    Board* board = BoardCreate(7, 7, "standard", 15);
    Coord alpha_body[] = {{3, 3}, {3, 2}, {3, 1}};
    Coord beta_body[] = {{5, 3}, {5, 2}, {5, 1}, {5, 0}};
    Snake alpha = make_snake("alpha", "Alpha", 88, alpha_body, 3);
    Snake beta = make_snake("beta", "Beta", 90, beta_body, 4);
    assert(BoardAddSnake(board, &alpha));
    assert(BoardAddSnake(board, &beta));
    SnakeFree(&alpha);
    SnakeFree(&beta);
    assert(BoardAddFood(board, (Coord){4, 3}));
    assert(BoardAddHazard(board, (Coord){3, 4}));
    return board;
}

static void test_feature_extraction_matches_expected_values(void) {
    Board* board = make_feature_board();
    CoreOpponentFeatureVector features;
    assert(CoreOpponentModelFeatures(board, "alpha", 12, 42, MOVE_RIGHT, &features) == CORE_OK);
    assert(features.values[CORE_OPPONENT_FEATURE_CANDIDATE_MOVE] == (double)MOVE_RIGHT);
    assert(features.values[CORE_OPPONENT_FEATURE_TURN] == 12.0);
    assert(features.values[CORE_OPPONENT_FEATURE_BOARD_WIDTH] == 7.0);
    assert(features.values[CORE_OPPONENT_FEATURE_BOARD_HEIGHT] == 7.0);
    assert(features.values[CORE_OPPONENT_FEATURE_ALIVE_SNAKES] == 2.0);
    assert(features.values[CORE_OPPONENT_FEATURE_SNAKE_RANK] == 42.0);
    assert(features.values[CORE_OPPONENT_FEATURE_SNAKE_HEALTH] == 88.0);
    assert(features.values[CORE_OPPONENT_FEATURE_SNAKE_LENGTH] == 3.0);
    assert(features.values[CORE_OPPONENT_FEATURE_CANDIDATE_IS_FOOD] == 1.0);
    assert(features.values[CORE_OPPONENT_FEATURE_CANDIDATE_IS_HAZARD] == 0.0);
    assert(features.values[CORE_OPPONENT_FEATURE_CANDIDATE_TO_NEAREST_FOOD] == 0.0);
    assert(features.values[CORE_OPPONENT_FEATURE_HEAD_TO_NEAREST_FOOD] == 1.0);
    BoardFree(board);
}

static void test_sigmoid_bounds(void) {
    assert(CoreOpponentModelSigmoid(0.0) > 0.499);
    assert(CoreOpponentModelSigmoid(0.0) < 0.501);
    assert(CoreOpponentModelSigmoid(10.0) > 0.999);
    assert(CoreOpponentModelSigmoid(-10.0) < 0.001);
}

static void test_normalize_scores(void) {
    double scores[4] = {2.0, 1.0, 1.0, 0.0};
    double probabilities[4] = {0.0, 0.0, 0.0, 0.0};
    CoreOpponentModelNormalizeScores(scores, probabilities);
    assert(fabs(probabilities[MOVE_UP] - 0.5) < 1e-12);
    assert(fabs(probabilities[MOVE_DOWN] - 0.25) < 1e-12);
    assert(fabs(probabilities[MOVE_LEFT] - 0.25) < 1e-12);
    assert(fabs(probabilities[MOVE_RIGHT] - 0.0) < 1e-12);
}

static void test_normalize_uniform_for_invalid_sum(void) {
    double scores[4] = {0.0, -1.0, 0.0, NAN};
    double probabilities[4] = {0.0, 0.0, 0.0, 0.0};
    CoreOpponentModelNormalizeScores(scores, probabilities);
    assert(fabs(probabilities[MOVE_UP] - 0.25) < 1e-12);
    assert(fabs(probabilities[MOVE_DOWN] - 0.25) < 1e-12);
    assert(fabs(probabilities[MOVE_LEFT] - 0.25) < 1e-12);
    assert(fabs(probabilities[MOVE_RIGHT] - 0.25) < 1e-12);
}

static void test_scores_return_four_finite_values(void) {
    Board* board = make_feature_board();
    double scores[4];
    assert(CoreOpponentModelScores(board, "alpha", 12, 42, scores) == CORE_OK);
    for (int i = 0; i < 4; ++i) {
        assert(isfinite(scores[i]));
        assert(scores[i] >= 0.0);
        assert(scores[i] <= 1.0);
    }
    BoardFree(board);
}

int main(void) {
    test_sigmoid_bounds();
    test_normalize_scores();
    test_normalize_uniform_for_invalid_sum();
    test_feature_extraction_matches_expected_values();
    test_scores_return_four_finite_values();
    puts("test_opponent_model OK");
    return 0;
}
```

- [ ] **Step 2: Run the C test and verify it fails**

Run:

```bash
tests/c/build_test_opponent_model.sh
./build/tests/test_opponent_model
```

Expected: FAIL because the current `CoreOpponentModelFeatures` only fills candidate move, turn, and rank.

- [ ] **Step 3: Implement C feature extraction**

Replace the `CoreOpponentModelFeatures` implementation in `battlesnake/c-core/core/opponent_model.c` with:

```c
static int manhattan(Coord a, Coord b) {
    int dx = a.x > b.x ? a.x - b.x : b.x - a.x;
    int dy = a.y > b.y ? a.y - b.y : b.y - a.y;
    return dx + dy;
}

static bool coord_equals(Coord a, Coord b) {
    return a.x == b.x && a.y == b.y;
}

static bool coord_in_array(const Coord* coords, int count, Coord target) {
    for (int i = 0; i < count; ++i) {
        if (coord_equals(coords[i], target)) {
            return true;
        }
    }
    return false;
}

static double nearest_distance(Coord point, const Coord* targets, int target_count) {
    if (target_count == 0) {
        return 99.0;
    }
    int best = manhattan(point, targets[0]);
    for (int i = 1; i < target_count; ++i) {
        int distance = manhattan(point, targets[i]);
        if (distance < best) {
            best = distance;
        }
    }
    return (double)best;
}

static double center_distance(const Board* board, Coord point) {
    double center_x = ((double)board->width - 1.0) / 2.0;
    double center_y = ((double)board->height - 1.0) / 2.0;
    return fabs((double)point.x - center_x) + fabs((double)point.y - center_y);
}

static int alive_snake_count(const Board* board) {
    int count = 0;
    for (int i = 0; i < board->snake_count; ++i) {
        if (board->snakes[i].body_count > 0) {
            count += 1;
        }
    }
    return count;
}

static int adjacent_head_count(const Board* board, const Snake* me, Coord point, bool longer_or_equal) {
    int count = 0;
    for (int i = 0; i < board->snake_count; ++i) {
        const Snake* other = &board->snakes[i];
        if (other == me || other->body_count == 0) {
            continue;
        }
        bool length_match = longer_or_equal ? other->length >= me->length : other->length < me->length;
        if (length_match && manhattan(point, other->body[0]) == 1) {
            count += 1;
        }
    }
    return count;
}

CoreStatus CoreOpponentModelFeatures(
    const Board* board,
    const char* snake_id,
    int turn,
    int snake_rank,
    MoveDirection candidate_move,
    CoreOpponentFeatureVector* out_features
) {
    if (board == NULL || snake_id == NULL || out_features == NULL) {
        return CORE_ERROR;
    }
    const Snake* snake = BoardFindSnakeConst(board, snake_id);
    if (snake == NULL || snake->body_count == 0) {
        return CORE_ERROR;
    }
    for (int i = 0; i < CORE_OPPONENT_FEATURE_COUNT; ++i) {
        out_features->values[i] = 0.0;
    }
    MoveDirection safe_moves[4];
    int safe_count = BoardSafeMoves((Board*)board, snake_id, safe_moves);
    bool is_safe = false;
    for (int i = 0; i < safe_count; ++i) {
        if (safe_moves[i] == candidate_move) {
            is_safe = true;
        }
    }
    Coord head = snake->body[0];
    Coord candidate = MoveStep(head, candidate_move);
    Coord occupied[512];
    int occupied_count = BoardOccupied(board, false, occupied, 512);
    bool in_bounds = BoardInBounds(board, candidate);

    out_features->values[CORE_OPPONENT_FEATURE_CANDIDATE_MOVE] = (double)candidate_move;
    out_features->values[CORE_OPPONENT_FEATURE_TURN] = (double)turn;
    out_features->values[CORE_OPPONENT_FEATURE_BOARD_WIDTH] = (double)board->width;
    out_features->values[CORE_OPPONENT_FEATURE_BOARD_HEIGHT] = (double)board->height;
    out_features->values[CORE_OPPONENT_FEATURE_ALIVE_SNAKES] = (double)alive_snake_count(board);
    out_features->values[CORE_OPPONENT_FEATURE_SNAKE_RANK] = snake_rank < 0 ? 999.0 : (double)snake_rank;
    out_features->values[CORE_OPPONENT_FEATURE_SNAKE_HEALTH] = (double)snake->health;
    out_features->values[CORE_OPPONENT_FEATURE_SNAKE_LENGTH] = (double)snake->length;
    out_features->values[CORE_OPPONENT_FEATURE_SAFE_MOVES_COUNT] = (double)safe_count;
    out_features->values[CORE_OPPONENT_FEATURE_CANDIDATE_IS_SAFE] = is_safe ? 1.0 : 0.0;
    out_features->values[CORE_OPPONENT_FEATURE_CANDIDATE_IN_BOUNDS] = in_bounds ? 1.0 : 0.0;
    out_features->values[CORE_OPPONENT_FEATURE_CANDIDATE_OCCUPIED_WITHOUT_TAILS] =
        coord_in_array(occupied, occupied_count, candidate) ? 1.0 : 0.0;
    out_features->values[CORE_OPPONENT_FEATURE_CANDIDATE_IS_FOOD] =
        coord_in_array(board->food, board->food_count, candidate) ? 1.0 : 0.0;
    out_features->values[CORE_OPPONENT_FEATURE_CANDIDATE_IS_HAZARD] =
        coord_in_array(board->hazards, board->hazard_count, candidate) ? 1.0 : 0.0;
    out_features->values[CORE_OPPONENT_FEATURE_CANDIDATE_TO_NEAREST_FOOD] =
        nearest_distance(candidate, board->food, board->food_count);
    out_features->values[CORE_OPPONENT_FEATURE_HEAD_TO_NEAREST_FOOD] =
        nearest_distance(head, board->food, board->food_count);
    out_features->values[CORE_OPPONENT_FEATURE_CANDIDATE_CENTER_DISTANCE] =
        in_bounds ? center_distance(board, candidate) : 99.0;
    int reachable = 0;
    if (is_safe && CoreReachableSpace(board, candidate, snake_id, &reachable) == CORE_OK) {
        out_features->values[CORE_OPPONENT_FEATURE_CANDIDATE_REACHABLE_SPACE] = (double)reachable;
    }
    out_features->values[CORE_OPPONENT_FEATURE_ADJACENT_LONGER_OR_EQUAL_HEADS] =
        (double)adjacent_head_count(board, snake, candidate, true);
    out_features->values[CORE_OPPONENT_FEATURE_ADJACENT_SHORTER_HEADS] =
        (double)adjacent_head_count(board, snake, candidate, false);
    return CORE_OK;
}
```

- [ ] **Step 4: Run the C test and verify it passes**

Run:

```bash
tests/c/build_test_opponent_model.sh
./build/tests/test_opponent_model
```

Expected output:

```text
test_opponent_model OK
```

- [ ] **Step 5: Commit**

Run:

```bash
git add battlesnake/c-core/core/opponent_model.c tests/c/test_opponent_model.c
git commit -m "feat: extract opponent model features in c"
```

---

### Task 5: Expose C Opponent Model Scores Through the Native Python Extension

**Files:**
- Modify: `battlesnake/c-core/py-core/py_core.c`
- Modify: `setup.py`
- Create: `tests/test_native_opponent_model.py`

- [ ] **Step 1: Add Python API tests**

Create `tests/test_native_opponent_model.py`:

```python
from __future__ import annotations

import math
import unittest

from battlesnake.battlesnake_native import (
    Board,
    Coord,
    Snake,
    opponent_model_probabilities,
    opponent_model_scores,
)


def sample_board() -> Board:
    return Board(
        width=7,
        height=7,
        snakes={
            "alpha": Snake("alpha", "Alpha", 88, [Coord(3, 3), Coord(3, 2), Coord(3, 1)], length=3),
            "beta": Snake("beta", "Beta", 90, [Coord(5, 3), Coord(5, 2), Coord(5, 1), Coord(5, 0)], length=4),
        },
        food=[Coord(4, 3)],
        hazards=[Coord(3, 4)],
        ruleset_name="standard",
        hazard_damage=15,
    )


class NativeOpponentModelTests(unittest.TestCase):
    def test_scores_returns_four_finite_scores(self) -> None:
        scores = opponent_model_scores(sample_board(), "alpha", turn=12, snake_rank=42)
        self.assertEqual(set(scores), {"up", "down", "left", "right"})
        for value in scores.values():
            self.assertTrue(math.isfinite(value))
            self.assertGreaterEqual(value, 0.0)
            self.assertLessEqual(value, 1.0)

    def test_probabilities_sum_to_one(self) -> None:
        probabilities = opponent_model_probabilities(sample_board(), "alpha", turn=12, snake_rank=42)
        self.assertEqual(set(probabilities), {"up", "down", "left", "right"})
        self.assertAlmostEqual(sum(probabilities.values()), 1.0, places=12)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the test and verify it fails**

Run:

```bash
python3 setup.py build_ext --inplace --force
python3 -m unittest tests.test_native_opponent_model -v
```

Expected: FAIL with `ImportError` for `opponent_model_scores`.

- [ ] **Step 3: Add source files to setup.py**

Modify `SOURCE_FILES` in `setup.py` by adding these entries immediately after `battlesnake/c-core/core/transposition_table.c`:

```python
    "battlesnake/c-core/core/opponent_model.c",
    "battlesnake/c-core/core/opponent_model_generated.c",
```

- [ ] **Step 4: Add Python wrappers**

Add helper function in `battlesnake/c-core/py-core/py_core.c` near `dict_set_double`:

```c
static int dict_set_move_double(PyObject* dict, MoveDirection move, double value) {
    return dict_set_double(dict, MoveDirectionToString(move), value);
}
```

Add wrapper functions before `PyCoreMethods`:

```c
static PyObject* py_opponent_model_scores(PyObject* self, PyObject* args, PyObject* kwargs) {
    (void)self;
    PyObject* board_obj = NULL;
    const char* snake_id = NULL;
    int turn = 0;
    int snake_rank = -1;
    static char* keywords[] = {"board", "snake_id", "turn", "snake_rank", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "Osi|i", keywords, &board_obj, &snake_id, &turn, &snake_rank)) {
        return NULL;
    }
    Board* board = board_from_pyobject(board_obj);
    if (board == NULL) {
        return NULL;
    }
    double scores[4];
    CoreStatus status = CoreOpponentModelScores(board, snake_id, turn, snake_rank, scores);
    if (status != CORE_OK) {
        return raise_for_status(status);
    }
    PyObject* result = PyDict_New();
    if (result == NULL) {
        return NULL;
    }
    for (int move = MOVE_UP; move <= MOVE_RIGHT; ++move) {
        if (dict_set_move_double(result, (MoveDirection)move, scores[move]) < 0) {
            Py_DECREF(result);
            return NULL;
        }
    }
    return result;
}

static PyObject* py_opponent_model_probabilities(PyObject* self, PyObject* args, PyObject* kwargs) {
    (void)self;
    PyObject* board_obj = NULL;
    const char* snake_id = NULL;
    int turn = 0;
    int snake_rank = -1;
    static char* keywords[] = {"board", "snake_id", "turn", "snake_rank", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "Osi|i", keywords, &board_obj, &snake_id, &turn, &snake_rank)) {
        return NULL;
    }
    Board* board = board_from_pyobject(board_obj);
    if (board == NULL) {
        return NULL;
    }
    double probabilities[4];
    CoreStatus status = CoreOpponentModelProbabilities(board, snake_id, turn, snake_rank, probabilities);
    if (status != CORE_OK) {
        return raise_for_status(status);
    }
    PyObject* result = PyDict_New();
    if (result == NULL) {
        return NULL;
    }
    for (int move = MOVE_UP; move <= MOVE_RIGHT; ++move) {
        if (dict_set_move_double(result, (MoveDirection)move, probabilities[move]) < 0) {
            Py_DECREF(result);
            return NULL;
        }
    }
    return result;
}
```

Add methods to `PyCoreMethods` before `board_hash`:

```c
    {"opponent_model_scores", (PyCFunction)py_opponent_model_scores, METH_VARARGS | METH_KEYWORDS, "Score four opponent candidate moves with the embedded model."},
    {"opponent_model_probabilities", (PyCFunction)py_opponent_model_probabilities, METH_VARARGS | METH_KEYWORDS, "Return normalized opponent move probabilities from the embedded model."},
```

- [ ] **Step 5: Build and run Python API tests**

Run:

```bash
python3 setup.py build_ext --inplace --force
python3 -m unittest tests.test_native_opponent_model -v
```

Expected: PASS.

- [ ] **Step 6: Run existing native/search tests**

Run:

```bash
python3 -m unittest tests.test_search_diagnostics tests.test_zobrist_hash tests.test_native_opponent_model -v
```

Expected: PASS.

- [ ] **Step 7: Commit**

Run:

```bash
git add setup.py battlesnake/c-core/py-core/py_core.c tests/test_native_opponent_model.py
git commit -m "feat: expose c opponent model through native extension"
```

---

### Task 6: Add Python-vs-C Feature and Score Parity Tests

**Files:**
- Create: `tests/test_native_opponent_model_parity.py`
- Modify: `battlesnake/c-core/py-core/py_core.c`

- [ ] **Step 1: Add a feature-vector wrapper test**

Create `tests/test_native_opponent_model_parity.py`:

```python
from __future__ import annotations

import unittest

from battlesnake.battlesnake_native import Board, Coord, Snake, opponent_model_features
from battlesnake.training.opponent_model.features import candidate_rows
from battlesnake.training.opponent_model.schema import MoveObservation


FEATURE_KEYS = [
    "candidate_move",
    "turn",
    "board_width",
    "board_height",
    "alive_snakes",
    "snake_rank",
    "snake_health",
    "snake_length",
    "safe_moves_count",
    "candidate_is_safe",
    "candidate_in_bounds",
    "candidate_occupied_without_tails",
    "candidate_is_food",
    "candidate_is_hazard",
    "candidate_to_nearest_food",
    "head_to_nearest_food",
    "candidate_center_distance",
    "candidate_reachable_space",
    "adjacent_longer_or_equal_heads",
    "adjacent_shorter_heads",
]


def sample_board() -> Board:
    return Board(
        width=7,
        height=7,
        snakes={
            "alpha": Snake("alpha", "Alpha", 88, [Coord(3, 3), Coord(3, 2), Coord(3, 1)], length=3),
            "beta": Snake("beta", "Beta", 90, [Coord(5, 3), Coord(5, 2), Coord(5, 1), Coord(5, 0)], length=4),
        },
        food=[Coord(4, 3)],
        hazards=[Coord(3, 4)],
        ruleset_name="standard",
        hazard_damage=15,
    )


class NativeOpponentModelParityTests(unittest.TestCase):
    def test_c_features_match_python_features_for_all_candidate_moves(self) -> None:
        board = sample_board()
        observation = MoveObservation(
            observation_id="game:12:alpha",
            game_id="game",
            split="test",
            turn=12,
            snake_id="alpha",
            snake_name="Alpha",
            snake_rank=42,
            target_move="right",
            board_width=7,
            board_height=7,
            alive_snakes=2,
        )
        python_rows = {row.candidate_move: row.features for row in candidate_rows(observation, board)}
        for move, python_features in python_rows.items():
            with self.subTest(move=move):
                c_features = opponent_model_features(board, "alpha", turn=12, snake_rank=42, candidate_move=move)
                for key in FEATURE_KEYS:
                    self.assertAlmostEqual(float(c_features[key]), float(python_features[key]), places=6, msg=key)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the parity test and verify it fails**

Run:

```bash
python3 setup.py build_ext --inplace --force
PYTHONPATH=.venv-ml/lib/python3.10/site-packages python3 -m unittest tests.test_native_opponent_model_parity -v
```

Expected: FAIL with `ImportError` for `opponent_model_features`.

- [ ] **Step 3: Add the feature-vector wrapper**

Add this wrapper before `py_opponent_model_scores` in `battlesnake/c-core/py-core/py_core.c`:

```c
static PyObject* py_opponent_model_features(PyObject* self, PyObject* args, PyObject* kwargs) {
    (void)self;
    PyObject* board_obj = NULL;
    const char* snake_id = NULL;
    const char* candidate_move_str = NULL;
    int turn = 0;
    int snake_rank = -1;
    static char* keywords[] = {"board", "snake_id", "turn", "snake_rank", "candidate_move", NULL};
    if (!PyArg_ParseTupleAndKeywords(
            args,
            kwargs,
            "Osiis",
            keywords,
            &board_obj,
            &snake_id,
            &turn,
            &snake_rank,
            &candidate_move_str
        )) {
        return NULL;
    }
    Board* board = board_from_pyobject(board_obj);
    if (board == NULL) {
        return NULL;
    }
    MoveDirection move = MoveDirectionFromString(candidate_move_str);
    if (move == MOVE_INVALID) {
        PyErr_SetString(PyExc_ValueError, "candidate_move must be one of up, down, left, right");
        return NULL;
    }
    CoreOpponentFeatureVector features;
    CoreStatus status = CoreOpponentModelFeatures(board, snake_id, turn, snake_rank, move, &features);
    if (status != CORE_OK) {
        return raise_for_status(status);
    }
    PyObject* result = PyDict_New();
    if (result == NULL) {
        return NULL;
    }
    for (int i = 0; i < CORE_OPPONENT_FEATURE_COUNT; ++i) {
        if (dict_set_double(result, CoreOpponentModelFeatureNames[i], features.values[i]) < 0) {
            Py_DECREF(result);
            return NULL;
        }
    }
    return result;
}
```

Add this method to `PyCoreMethods` before `opponent_model_scores`:

```c
    {"opponent_model_features", (PyCFunction)py_opponent_model_features, METH_VARARGS | METH_KEYWORDS, "Return C opponent model features for one candidate move."},
```

- [ ] **Step 4: Build and run parity tests**

Run:

```bash
python3 setup.py build_ext --inplace --force
PYTHONPATH=.venv-ml/lib/python3.10/site-packages python3 -m unittest tests.test_native_opponent_model_parity tests.test_native_opponent_model -v
```

Expected: PASS.

- [ ] **Step 5: Commit**

Run:

```bash
git add battlesnake/c-core/py-core/py_core.c tests/test_native_opponent_model_parity.py
git commit -m "test: verify c opponent model feature parity"
```

---

### Task 7: Add C-Core Model to Native Server Build

**Files:**
- Modify: `tools/build_native_server.sh`

- [ ] **Step 1: Modify native server build script**

Add these source files after `battlesnake/c-core/core/transposition_table.c` in `tools/build_native_server.sh`:

```bash
  battlesnake/c-core/core/opponent_model.c \
  battlesnake/c-core/core/opponent_model_generated.c \
```

- [ ] **Step 2: Build the native server**

Run:

```bash
tools/build_native_server.sh
```

Expected:

- command exits with code `0`;
- `build/battlesnake-server` exists.

- [ ] **Step 3: Run C and Python smoke tests after server build**

Run:

```bash
tests/c/build_test_opponent_model.sh
./build/tests/test_opponent_model
python3 -m unittest tests.test_native_opponent_model -v
```

Expected:

- C test prints `test_opponent_model OK`;
- Python native opponent model test passes.

- [ ] **Step 4: Commit**

Run:

```bash
git add tools/build_native_server.sh
git commit -m "build: include opponent model in native server"
```

---

### Task 8: Add Production-Oriented Native Latency Benchmark

**Files:**
- Create: `tools/benchmark_native_opponent_model.py`
- Modify: `docs/opponent-model-runtime-report.md`

- [ ] **Step 1: Add benchmark script**

Create `tools/benchmark_native_opponent_model.py`:

```python
#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import statistics
import time

from battlesnake.battlesnake_native import Board, Coord, Snake, opponent_model_probabilities


def sample_board() -> Board:
    return Board(
        width=11,
        height=11,
        snakes={
            "me": Snake("me", "Me", 90, [Coord(5, 5), Coord(5, 4), Coord(5, 3)], length=3),
            "alpha": Snake("alpha", "Alpha", 88, [Coord(2, 2), Coord(2, 1), Coord(2, 0)], length=3),
            "beta": Snake("beta", "Beta", 90, [Coord(8, 8), Coord(8, 9), Coord(8, 10)], length=3),
            "gamma": Snake("gamma", "Gamma", 77, [Coord(2, 8), Coord(1, 8), Coord(0, 8)], length=3),
        },
        food=[Coord(3, 2), Coord(7, 8), Coord(5, 6)],
        hazards=[],
        ruleset_name="standard",
        hazard_damage=15,
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Benchmark embedded C opponent model inference.")
    parser.add_argument("--iterations", type=int, default=10000)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    board = sample_board()
    opponents = ["alpha", "beta", "gamma"]
    for _ in range(100):
        for opponent in opponents:
            opponent_model_probabilities(board, opponent, turn=20, snake_rank=-1)
    times_us = []
    for _ in range(args.iterations):
        start = time.perf_counter_ns()
        for opponent in opponents:
            opponent_model_probabilities(board, opponent, turn=20, snake_rank=-1)
        times_us.append((time.perf_counter_ns() - start) / 1000.0)
    payload = {
        "opponents": len(opponents),
        "iterations": args.iterations,
        "median_us": statistics.median(times_us),
        "p95_us": sorted(times_us)[int(args.iterations * 0.95) - 1],
        "p99_us": sorted(times_us)[int(args.iterations * 0.99) - 1],
    }
    print(json.dumps(payload, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

- [ ] **Step 2: Make benchmark executable**

Run:

```bash
chmod +x tools/benchmark_native_opponent_model.py
```

Expected: command exits with code `0`.

- [ ] **Step 3: Run benchmark**

Run:

```bash
python3 setup.py build_ext --inplace --force
python3 tools/benchmark_native_opponent_model.py --iterations 10000 --output ai-artifacts/opponent-model/native-opponent-model-benchmark.json
cat ai-artifacts/opponent-model/native-opponent-model-benchmark.json
```

Expected:

- stdout is JSON and matches `ai-artifacts/opponent-model/native-opponent-model-benchmark.json`;
- `median_us` is below `1000.0` for three opponents on local development hardware;
- `p95_us` is below `2000.0` for three opponents on local development hardware.

- [ ] **Step 4: Update runtime report**

Run this script to insert the measured JSON into `docs/opponent-model-runtime-report.md` under `Runtime Latency`:

````bash
python3 - <<'PY'
import json
from pathlib import Path

result_path = Path("ai-artifacts/opponent-model/native-opponent-model-benchmark.json")
report_path = Path("docs/opponent-model-runtime-report.md")
result = json.loads(result_path.read_text())
section = """### Embedded C Runtime

The embedded C benchmark measures feature extraction plus model inference through
the native extension for three Standard FFA opponents. This is the closest
Python-accessible proxy for production `c-core` latency.

Measured command:

```bash
python3 tools/benchmark_native_opponent_model.py --iterations 10000 --output ai-artifacts/opponent-model/native-opponent-model-benchmark.json
```

Measured result:

```json
""" + json.dumps(result, indent=2, sort_keys=True) + """
```
"""
text = report_path.read_text()
next_heading = "### Compute Node"
if "### Embedded C Runtime\n" in text:
    start = text.index("### Embedded C Runtime\n")
    end = text.index(next_heading, start)
    text = text[:start] + section + "\n" + text[end:]
else:
    text = text.replace(next_heading, section + "\n" + next_heading)
report_path.write_text(text)
print("updated docs/opponent-model-runtime-report.md")
PY
````

- [ ] **Step 5: Commit**

Run:

```bash
git add tools/benchmark_native_opponent_model.py ai-artifacts/opponent-model/native-opponent-model-benchmark.json docs/opponent-model-runtime-report.md
git commit -m "bench: measure embedded c opponent model latency"
```

---

### Task 9: Add Final Documentation and PR Notes

**Files:**
- Modify: `docs/opponent-model-runtime-report.md`
- Modify: `docs/opponent-model-training.md`

- [ ] **Step 1: Add production guidance to runtime report**

Add this section to `docs/opponent-model-runtime-report.md` after `Recommended Runtime Integration`:

```markdown
## C-Core Production Path

The production path should use the generated C model arrays and
`CoreOpponentModelProbabilities`. Python, joblib, pandas, and LightGBM must not
be imported by the native server or by request handling code.

Recommended call shape:

```c
double opponent_probabilities[4];
CoreStatus status = CoreOpponentModelProbabilities(
    board,
    opponent_snake_id,
    turn,
    snake_rank_or_minus_one,
    opponent_probabilities
);
```

The first Standard FFA integration should call this once per alive opponent at
the root board state. It should use the resulting distributions to order or
weight a small number of opponent joint responses. It should keep deterministic
safety checks as hard constraints and must not discard lethal low-probability
responses.
```

- [ ] **Step 2: Add model export note to training report**

Add this section to `docs/opponent-model-training.md` after `Compute Run`:

```markdown
## Runtime Export

The selected LightGBM model is published as
`ai-artifacts/opponent-model/gbdt_lightgbm.joblib.gz`. The production C path
does not load this archive directly. It is an input to
`tools/export_lightgbm_opponent_model.py` and `tools/generate_c_opponent_model.py`,
which produce static C arrays under `battlesnake/c-core/core/`.
```

- [ ] **Step 3: Run documentation checks**

Run:

```bash
python3 - <<'PY'
from pathlib import Path
runtime = Path("docs/opponent-model-runtime-report.md").read_text()
training = Path("docs/opponent-model-training.md").read_text()
required = [
    "CoreOpponentModelProbabilities",
    "Python, joblib, pandas, and LightGBM must not",
    "tools/export_lightgbm_opponent_model.py",
    "tools/generate_c_opponent_model.py",
]
missing = [item for item in required if item not in runtime + training]
print("missing", missing)
raise SystemExit(1 if missing else 0)
PY
```

Expected:

```text
missing []
```

- [ ] **Step 4: Run final tests**

Run:

```bash
PYTHONPATH=.venv-ml/lib/python3.10/site-packages python3 -m unittest discover -s tests/training -p 'test_opponent_*.py' -v
python3 -m unittest tests.test_native_opponent_model tests.test_native_opponent_model_parity -v
tests/c/build_test_opponent_model.sh
./build/tests/test_opponent_model
tools/build_native_server.sh
```

Expected:

- training tests pass;
- native opponent model tests pass;
- C opponent model test prints `test_opponent_model OK`;
- native server build succeeds.

- [ ] **Step 5: Commit**

Run:

```bash
git add docs/opponent-model-runtime-report.md docs/opponent-model-training.md
git commit -m "docs: describe c-core opponent model runtime path"
```

---

## Self-Review

Spec coverage:

- Minimal production-latency path: Tasks 2, 3, 4, 7, and 8 implement static C arrays, no dynamic Python/LightGBM dependency, native server build integration, and latency measurement.
- Use from `c-core`: Tasks 2, 4, 5, and 7 add public C API and compile it into both native extension and native server.
- Preserve offline model quality: Task 1 exports the selected model, Task 3 converts that model, Task 6 checks Python/C feature parity, and Task 8 measures C runtime latency.
- PR/issue handoff: this plan is committed under `docs/superpowers/plans/` and linked from the GitHub issue.

Placeholder scan:

- This plan intentionally avoids open-ended implementation phrases. Every task includes files, code, commands, expected outputs, and commit instructions.

Type consistency:

- `CoreOpponentFeatureVector`, `CoreOpponentTreeNode`, `CoreOpponentTree`, `CoreOpponentModelScoreFeatures`, `CoreOpponentModelScores`, and `CoreOpponentModelProbabilities` are defined before later tasks reference them.
- Feature indices match the offline `FEATURE_COLUMNS` ordering used by the current selected model.
