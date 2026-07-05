# Custom C Battlesnake HTTP Server Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the FastAPI/Uvicorn runtime with a custom C HTTP/1.1 server that handles only the Battlesnake API routes and calls the existing native search core directly.

**Architecture:** Build a small single-purpose C executable beside the existing CPython extension. The server accepts plain HTTP on one TCP port, parses only `GET /`, `POST /start`, `POST /move`, and `POST /end`, converts request JSON directly into existing `Board`, `Snake`, `Coord`, and `MoveDirection` structs, calls the C strategy path, and writes fixed JSON responses. Python remains for tests, benchmark orchestration, and optional development comparison until the native server passes equivalence and performance gates.

**Tech Stack:** C17/C2x-compatible C, POSIX sockets, existing `battlesnake/c-core` datatypes and search code, Python `unittest`/benchmark scripts for validation, Docker multi-stage build with a native runtime image.

---

## Source Context

The Battlesnake docs define four required webhooks:

- `GET /` returns Battlesnake metadata with `apiversion`, `author`, `color`, `head`, `tail`, and `version`.
- `POST /start` receives `game`, `turn`, `board`, and `you`; the response is ignored.
- `POST /move` receives the same state and must return `{"move":"up"}` or one of `down`, `left`, `right`.
- `POST /end` receives final state and the response is ignored.

The current repo path is:

- Python HTTP entrypoint: `battlesnake/main.py`
- API models: `battlesnake/types.py`
- Python-to-native board conversion: `battlesnake/game.py`
- Duel strategy path: `battlesnake/strategies/duel.py` -> `battlesnake/core/minimax.py` -> `battlesnake.battlesnake_native.minimax_move`
- Native board/search APIs: `battlesnake/c-core/datatypes/board.h`, `battlesnake/c-core/datatypes/snake.h`, `battlesnake/c-core/core/core_algorithms.h`
- Existing C source list for the Python extension: `setup.py`
- Existing benchmark fixtures: `benchmarks/scenarios.py`

## File Structure

Create native server code under `battlesnake/c-core/server/`:

- `battlesnake/c-core/server/arena.h`
  Request-scoped bump allocator used by JSON parsing and response assembly.
- `battlesnake/c-core/server/arena.c`
  Fixed-capacity allocation, reset, and overflow reporting.
- `battlesnake/c-core/server/battlesnake_json.h`
  Public parser API from request body bytes to a native game request.
- `battlesnake/c-core/server/battlesnake_json.c`
  Battlesnake-specific JSON scanner. It parses only the fields this snake uses and skips unknown values.
- `battlesnake/c-core/server/battlesnake_strategy.h`
  Public strategy API used by the HTTP layer.
- `battlesnake/c-core/server/battlesnake_strategy.c`
  Native replacement for `select_strategy`, `fallback_move`, and duel minimax dispatch.
- `battlesnake/c-core/server/battlesnake_http.h`
  Public HTTP parser and response writer API.
- `battlesnake/c-core/server/battlesnake_http.c`
  Minimal HTTP/1.1 request parser and route dispatcher.
- `battlesnake/c-core/server/server_main.c`
  Socket accept loop, per-request lifecycle, environment parsing, and signal shutdown.

Create C tests under `tests/c/`:

- `tests/c/test_battlesnake_json.c`
  Parser tests for example move payloads, unknown fields, arrays, and malformed bodies.
- `tests/c/test_battlesnake_strategy.c`
  Strategy equivalence tests for fallback and two-snake minimax dispatch.
- `tests/c/test_battlesnake_http.c`
  Raw HTTP request/response parsing tests.
- `tools/run_c_server_tests.sh`
  Builds and runs the new C test binaries.

Create benchmark/migration tooling:

- `benchmarks/battlesnake_payloads.py`
  Converts existing benchmark scenarios into Battlesnake JSON payload strings.
- `benchmarks/bench_http_runtime.py`
  Starts FastAPI and native server variants, sends raw socket requests, records latency and RSS.
- `benchmarks/results/http-runtime-baseline.jsonl`
  Generated benchmark artifact; do not hand edit after creation.
- `requirements-dev.txt`
  Development-only Python dependencies for the reference FastAPI app and benchmark comparator.
- `tools/build_native_server.sh`
  Deterministic native server build command.

Modify existing files:

- `setup.py`
  Share the C source list between the Python extension and native-server build notes; remove production runtime dependency on FastAPI/Uvicorn/Pydantic after native runtime migration.
- `battlesnake/Dockerfile`
  Build and run the native executable instead of Uvicorn after migration gates pass.
- `battlesnake/requirements.txt`
  Keep production runtime Python dependencies empty after Docker switches to native.
- `README.md`
  Document local native build, native run command, and benchmark gate.
- `docs/runbooks/battlesnake-deploy.md`
  Replace Uvicorn deployment instructions with native server deployment instructions.

## Design Constraints

- Keep the native HTTP server plain HTTP. TLS termination belongs to the platform in front of the container.
- Support HTTP/1.0 and HTTP/1.1 request lines.
- Require `Content-Length` for `POST` routes.
- Reject chunked transfer encoding with `501 Not Implemented`.
- Limit request headers to 16 KiB and request bodies to 128 KiB by default.
- Keep one request-scoped arena per connection handler and reset it after every request.
- Treat unknown JSON object fields as ignorable.
- Fail malformed JSON or missing required move fields with `400 Bad Request`.
- Return `404 Not Found` for unknown routes.
- Return `405 Method Not Allowed` for known route with wrong method.
- Return `413 Payload Too Large` when body exceeds `BATTLESNAKE_MAX_BODY_BYTES`.
- Return `500 Internal Server Error` only for allocation failure or core search failure.
- Do not use a general HTTP or JSON library in the native runtime path.
- Preserve current gameplay behavior first: non-duel and unimplemented strategy modes use first safe move fallback; two-snake `solo` games use `CoreMinimaxMove`.

## Task 1: Native Build Skeleton

**Files:**
- Create: `tools/build_native_server.sh`
- Create: `battlesnake/c-core/server/server_main.c`
- Modify: `setup.py`
- Test: shell build command

- [ ] **Step 1: Create a native build script**

Write `tools/build_native_server.sh`:

```bash
#!/usr/bin/env bash
set -euo pipefail

mkdir -p build

gcc \
  -std=c2x \
  -D_POSIX_C_SOURCE=200809L \
  -O3 \
  -DNDEBUG \
  -Ibattlesnake/c-core \
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
  battlesnake/c-core/server/arena.c \
  battlesnake/c-core/server/battlesnake_json.c \
  battlesnake/c-core/server/battlesnake_strategy.c \
  battlesnake/c-core/server/battlesnake_http.c \
  battlesnake/c-core/server/server_main.c \
  -lm \
  -o build/battlesnake-server
```

- [ ] **Step 2: Create a temporary server main so the build target has an entrypoint**

Write `battlesnake/c-core/server/server_main.c`:

```c
#include <stdio.h>

int main(void) {
    puts("battlesnake native server skeleton");
    return 0;
}
```

- [ ] **Step 3: Run the build and confirm the expected missing-file failure**

Run:

```bash
bash tools/build_native_server.sh
```

Expected: FAIL with compiler errors naming missing `battlesnake/c-core/server/arena.c`, `battlesnake_json.c`, `battlesnake_strategy.c`, or `battlesnake_http.c`.

- [ ] **Step 4: Add the executable build path to `setup.py` comments without changing Python extension behavior**

Modify the top of `setup.py` so it starts with this comment block before imports:

```python
"""Build metadata for the Python extension.

The native HTTP server is built by tools/build_native_server.sh from the same
C core files plus battlesnake/c-core/server/*.c. Keep SOURCE_FILES limited to
the CPython extension sources so `pip install .` continues to work.
"""
```

- [ ] **Step 5: Commit**

```bash
git add setup.py tools/build_native_server.sh battlesnake/c-core/server/server_main.c
git commit -m "build: add native battlesnake server target"
```

## Task 2: Request Arena

**Files:**
- Create: `battlesnake/c-core/server/arena.h`
- Create: `battlesnake/c-core/server/arena.c`
- Create: `tests/c/test_arena.c`
- Modify: `tools/run_c_server_tests.sh`
- Test: `tests/c/test_arena.c`

- [ ] **Step 1: Write the failing arena test**

Create `tests/c/test_arena.c`:

```c
#include "../../battlesnake/c-core/server/arena.h"

#include <assert.h>
#include <string.h>

static void test_alloc_and_reset(void) {
    BsArena arena;
    assert(BsArenaInit(&arena, 64));
    char* first = (char*)BsArenaAlloc(&arena, 8);
    assert(first != 0);
    memcpy(first, "abc", 4);
    assert(strcmp(first, "abc") == 0);
    BsArenaReset(&arena);
    char* second = (char*)BsArenaAlloc(&arena, 8);
    assert(second == first);
    BsArenaFree(&arena);
}

static void test_overflow_returns_null(void) {
    BsArena arena;
    assert(BsArenaInit(&arena, 16));
    assert(BsArenaAlloc(&arena, 32) == 0);
    assert(BsArenaHadOverflow(&arena));
    BsArenaFree(&arena);
}

int main(void) {
    test_alloc_and_reset();
    test_overflow_returns_null();
    return 0;
}
```

- [ ] **Step 2: Create the C test runner**

Write `tools/run_c_server_tests.sh`:

```bash
#!/usr/bin/env bash
set -euo pipefail

mkdir -p build/tests

gcc -std=c2x -D_POSIX_C_SOURCE=200809L -Ibattlesnake/c-core \
  tests/c/test_arena.c \
  battlesnake/c-core/server/arena.c \
  -o build/tests/test_arena

build/tests/test_arena
```

- [ ] **Step 3: Run the test and confirm it fails before implementation**

Run:

```bash
bash tools/run_c_server_tests.sh
```

Expected: FAIL with missing `arena.h` or `BsArena` symbols.

- [ ] **Step 4: Implement the arena header**

Write `battlesnake/c-core/server/arena.h`:

```c
#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    unsigned char* data;
    size_t capacity;
    size_t offset;
    bool overflowed;
} BsArena;

bool BsArenaInit(BsArena* arena, size_t capacity);
void BsArenaReset(BsArena* arena);
void BsArenaFree(BsArena* arena);
void* BsArenaAlloc(BsArena* arena, size_t size);
char* BsArenaStrDup(BsArena* arena, const char* start, size_t length);
bool BsArenaHadOverflow(const BsArena* arena);
```

- [ ] **Step 5: Implement the arena source**

Write `battlesnake/c-core/server/arena.c`:

```c
#include "arena.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

bool BsArenaInit(BsArena* arena, size_t capacity) {
    if (arena == 0 || capacity == 0) {
        return false;
    }
    arena->data = (unsigned char*)malloc(capacity);
    arena->capacity = capacity;
    arena->offset = 0;
    arena->overflowed = arena->data == 0;
    return arena->data != 0;
}

void BsArenaReset(BsArena* arena) {
    if (arena == 0) {
        return;
    }
    arena->offset = 0;
    arena->overflowed = false;
}

void BsArenaFree(BsArena* arena) {
    if (arena == 0) {
        return;
    }
    free(arena->data);
    arena->data = 0;
    arena->capacity = 0;
    arena->offset = 0;
    arena->overflowed = false;
}

void* BsArenaAlloc(BsArena* arena, size_t size) {
    if (arena == 0 || arena->data == 0) {
        return 0;
    }
    size_t aligned = (size + 7u) & ~((size_t)7u);
    if (aligned > arena->capacity || arena->offset > arena->capacity - aligned) {
        arena->overflowed = true;
        return 0;
    }
    void* result = arena->data + arena->offset;
    arena->offset += aligned;
    memset(result, 0, aligned);
    return result;
}

char* BsArenaStrDup(BsArena* arena, const char* start, size_t length) {
    char* result = (char*)BsArenaAlloc(arena, length + 1);
    if (result == 0) {
        return 0;
    }
    memcpy(result, start, length);
    result[length] = '\0';
    return result;
}

bool BsArenaHadOverflow(const BsArena* arena) {
    return arena != 0 && arena->overflowed;
}
```

- [ ] **Step 6: Run tests**

Run:

```bash
bash tools/run_c_server_tests.sh
```

Expected: PASS with exit code 0.

- [ ] **Step 7: Commit**

```bash
git add battlesnake/c-core/server/arena.h battlesnake/c-core/server/arena.c tests/c/test_arena.c tools/run_c_server_tests.sh
git commit -m "feat: add native request arena"
```

## Task 3: Battlesnake JSON Parser

**Files:**
- Create: `battlesnake/c-core/server/battlesnake_json.h`
- Create: `battlesnake/c-core/server/battlesnake_json.c`
- Create: `tests/c/test_battlesnake_json.c`
- Modify: `tools/run_c_server_tests.sh`
- Test: `tests/c/test_battlesnake_json.c`

- [ ] **Step 1: Write parser API and fixture-driven tests**

Create `battlesnake/c-core/server/battlesnake_json.h`:

```c
#pragma once

#include "../datatypes/board.h"
#include "arena.h"

#include <stddef.h>

typedef enum {
    BS_JSON_OK = 0,
    BS_JSON_MALFORMED = 1,
    BS_JSON_MISSING_REQUIRED = 2,
    BS_JSON_NO_MEMORY = 3,
} BsJsonStatus;

typedef struct {
    char* game_id;
    int turn;
    char* you_id;
    Board* board;
    int timeout_ms;
} BsGameRequest;

BsJsonStatus BsParseGameRequest(const char* body, size_t body_len, BsArena* arena, BsGameRequest* out_request);
void BsGameRequestFree(BsGameRequest* request);
const char* BsJsonStatusText(BsJsonStatus status);
```

Create `tests/c/test_battlesnake_json.c`:

```c
#include "../../battlesnake/c-core/server/battlesnake_json.h"

#include <assert.h>
#include <string.h>

static const char* MOVE_BODY =
    "{"
    "\"game\":{\"id\":\"game-1\",\"ruleset\":{\"name\":\"solo\",\"version\":\"v1\",\"settings\":{\"hazardDamagePerTurn\":14}},\"timeout\":500},"
    "\"turn\":14,"
    "\"board\":{\"height\":7,\"width\":7,\"food\":[{\"x\":3,\"y\":3}],\"hazards\":[{\"x\":0,\"y\":0}],"
    "\"snakes\":["
    "{\"id\":\"me\",\"name\":\"Me\",\"health\":90,\"body\":[{\"x\":1,\"y\":3},{\"x\":1,\"y\":2},{\"x\":1,\"y\":1}],\"head\":{\"x\":1,\"y\":3},\"length\":3},"
    "{\"id\":\"you\",\"name\":\"You\",\"health\":88,\"body\":[{\"x\":5,\"y\":3},{\"x\":5,\"y\":2},{\"x\":5,\"y\":1}],\"head\":{\"x\":5,\"y\":3},\"length\":3}"
    "]},"
    "\"you\":{\"id\":\"me\",\"name\":\"Me\",\"health\":90,\"body\":[{\"x\":1,\"y\":3},{\"x\":1,\"y\":2},{\"x\":1,\"y\":1}],\"head\":{\"x\":1,\"y\":3},\"length\":3}"
    "}";

static void test_parse_complete_move_body(void) {
    BsArena arena;
    BsGameRequest request;
    assert(BsArenaInit(&arena, 65536));
    assert(BsParseGameRequest(MOVE_BODY, strlen(MOVE_BODY), &arena, &request) == BS_JSON_OK);
    assert(strcmp(request.game_id, "game-1") == 0);
    assert(strcmp(request.you_id, "me") == 0);
    assert(request.turn == 14);
    assert(request.timeout_ms == 500);
    assert(request.board != 0);
    assert(request.board->width == 7);
    assert(request.board->height == 7);
    assert(strcmp(request.board->ruleset_name, "solo") == 0);
    assert(request.board->hazard_damage == 14);
    assert(request.board->snake_count == 2);
    assert(request.board->food_count == 1);
    assert(request.board->hazard_count == 1);
    assert(strcmp(request.board->snakes[0].id, "me") == 0);
    assert(request.board->snakes[0].body_len == 3);
    assert(request.board->snakes[0].body[0].x == 1);
    assert(request.board->snakes[0].body[0].y == 3);
    BsGameRequestFree(&request);
    BsArenaFree(&arena);
}

static void test_missing_you_id_is_rejected(void) {
    const char* body = "{\"game\":{\"id\":\"g\"},\"turn\":0,\"board\":{\"height\":1,\"width\":1,\"snakes\":[]},\"you\":{}}";
    BsArena arena;
    BsGameRequest request;
    assert(BsArenaInit(&arena, 4096));
    assert(BsParseGameRequest(body, strlen(body), &arena, &request) == BS_JSON_MISSING_REQUIRED);
    BsArenaFree(&arena);
}

static void test_malformed_body_is_rejected(void) {
    const char* body = "{\"game\":";
    BsArena arena;
    BsGameRequest request;
    assert(BsArenaInit(&arena, 4096));
    assert(BsParseGameRequest(body, strlen(body), &arena, &request) == BS_JSON_MALFORMED);
    BsArenaFree(&arena);
}

int main(void) {
    test_parse_complete_move_body();
    test_missing_you_id_is_rejected();
    test_malformed_body_is_rejected();
    return 0;
}
```

- [ ] **Step 2: Wire parser tests into `tools/run_c_server_tests.sh`**

Extend `tools/run_c_server_tests.sh` with:

```bash
gcc -std=c2x -D_POSIX_C_SOURCE=200809L -Ibattlesnake/c-core \
  tests/c/test_battlesnake_json.c \
  battlesnake/c-core/server/arena.c \
  battlesnake/c-core/server/battlesnake_json.c \
  battlesnake/c-core/datatypes/coord.c \
  battlesnake/c-core/datatypes/snake.c \
  battlesnake/c-core/datatypes/board.c \
  -o build/tests/test_battlesnake_json

build/tests/test_battlesnake_json
```

- [ ] **Step 3: Run tests and confirm parser implementation is missing**

Run:

```bash
bash tools/run_c_server_tests.sh
```

Expected: FAIL with missing `BsParseGameRequest`.

- [ ] **Step 4: Implement JSON scanner behavior**

Implement `battlesnake/c-core/server/battlesnake_json.c` with these exact externally visible behaviors:

```c
const char* BsJsonStatusText(BsJsonStatus status) {
    switch (status) {
        case BS_JSON_OK:
            return "ok";
        case BS_JSON_MALFORMED:
            return "malformed json";
        case BS_JSON_MISSING_REQUIRED:
            return "missing required field";
        case BS_JSON_NO_MEMORY:
            return "out of memory";
    }
    return "unknown json status";
}
```

The parser must fill:

```c
request->game_id = parsed game.id;
request->turn = parsed turn or 0;
request->you_id = parsed you.id;
request->timeout_ms = parsed game.timeout or 500;
request->board = BoardCreate(width, height, ruleset_name, hazard_damage);
```

Parsing rules:

- Required fields: `game.id`, `board.width`, `board.height`, `board.snakes`, `you.id`.
- Default `ruleset.name` to `"standard"` when absent.
- Default `hazardDamagePerTurn` to `15` when absent.
- Accept `food` and `hazards` as absent or empty arrays.
- For each board snake, require `id` and `body`; default `name` to `""`, `health` to `100`, and `length` to body length.
- Ignore `latency`, `head`, `shout`, `squad`, and `customizations`.
- Skip any unknown JSON field by recursively skipping its value.
- Allocate temporary strings and coordinate arrays from `BsArena`.
- Allocate the final `Board` through existing `BoardCreate`, `BoardAddSnake`, `BoardAddFood`, and `BoardAddHazard`.
- Return `BS_JSON_NO_MEMORY` if arena allocation or board allocation fails.

- [ ] **Step 5: Run parser tests**

Run:

```bash
bash tools/run_c_server_tests.sh
```

Expected: PASS with exit code 0.

- [ ] **Step 6: Commit**

```bash
git add battlesnake/c-core/server/battlesnake_json.h battlesnake/c-core/server/battlesnake_json.c tests/c/test_battlesnake_json.c tools/run_c_server_tests.sh
git commit -m "feat: parse battlesnake json in native server"
```

## Task 4: Native Strategy Bridge

**Files:**
- Create: `battlesnake/c-core/server/battlesnake_strategy.h`
- Create: `battlesnake/c-core/server/battlesnake_strategy.c`
- Create: `tests/c/test_battlesnake_strategy.c`
- Modify: `tools/run_c_server_tests.sh`
- Test: `tests/c/test_battlesnake_strategy.c`

- [ ] **Step 1: Write strategy API**

Create `battlesnake/c-core/server/battlesnake_strategy.h`:

```c
#pragma once

#include "../datatypes/board.h"

typedef struct {
    int default_time_budget_ms;
} BsStrategyConfig;

typedef enum {
    BS_STRATEGY_OK = 0,
    BS_STRATEGY_FALLBACK_USED = 1,
    BS_STRATEGY_ERROR = 2,
} BsStrategyStatus;

BsStrategyConfig BsStrategyConfigDefault(void);
BsStrategyStatus BsChooseMove(const Board* board, const char* snake_id, const BsStrategyConfig* config, MoveDirection* out_move);
```

- [ ] **Step 2: Write failing strategy tests**

Create `tests/c/test_battlesnake_strategy.c`:

```c
#include "../../battlesnake/c-core/server/battlesnake_strategy.h"

#include <assert.h>

static Snake make_snake(const char* id, Coord* body, int body_len, int health) {
    Snake snake;
    SnakeInit(&snake, id, id, health, body, body_len);
    snake.length = body_len;
    return snake;
}

static void test_single_snake_uses_safe_fallback(void) {
    Board* board = BoardCreate(5, 5, "standard", 0);
    Coord body[] = {{2, 2}, {2, 1}, {2, 0}};
    Snake snake = make_snake("me", body, 3, 90);
    MoveDirection move = MOVE_INVALID;
    BsStrategyConfig config = BsStrategyConfigDefault();
    assert(BoardAddSnake(board, &snake));
    assert(BsChooseMove(board, "me", &config, &move) == BS_STRATEGY_FALLBACK_USED);
    assert(move == MOVE_UP || move == MOVE_DOWN || move == MOVE_LEFT || move == MOVE_RIGHT);
    SnakeFree(&snake);
    BoardFree(board);
}

static void test_missing_snake_is_error(void) {
    Board* board = BoardCreate(5, 5, "standard", 0);
    MoveDirection move = MOVE_INVALID;
    BsStrategyConfig config = BsStrategyConfigDefault();
    assert(BsChooseMove(board, "me", &config, &move) == BS_STRATEGY_ERROR);
    BoardFree(board);
}

static void test_solo_two_snakes_uses_minimax(void) {
    Board* board = BoardCreate(7, 7, "solo", 0);
    Coord me_body[] = {{1, 3}, {1, 2}, {1, 1}};
    Coord you_body[] = {{5, 3}, {5, 2}, {5, 1}};
    Snake me = make_snake("me", me_body, 3, 90);
    Snake you = make_snake("you", you_body, 3, 90);
    MoveDirection move = MOVE_INVALID;
    BsStrategyConfig config = BsStrategyConfigDefault();
    config.default_time_budget_ms = 25;
    assert(BoardAddSnake(board, &me));
    assert(BoardAddSnake(board, &you));
    assert(BoardAddFood(board, (Coord){3, 3}));
    assert(BsChooseMove(board, "me", &config, &move) == BS_STRATEGY_OK);
    assert(move == MOVE_UP || move == MOVE_DOWN || move == MOVE_LEFT || move == MOVE_RIGHT);
    SnakeFree(&me);
    SnakeFree(&you);
    BoardFree(board);
}

int main(void) {
    test_single_snake_uses_safe_fallback();
    test_missing_snake_is_error();
    test_solo_two_snakes_uses_minimax();
    return 0;
}
```

- [ ] **Step 3: Wire strategy tests**

Extend `tools/run_c_server_tests.sh` with:

```bash
gcc -std=c2x -D_POSIX_C_SOURCE=200809L -Ibattlesnake/c-core \
  tests/c/test_battlesnake_strategy.c \
  battlesnake/c-core/server/battlesnake_strategy.c \
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
  -lm \
  -o build/tests/test_battlesnake_strategy

build/tests/test_battlesnake_strategy
```

- [ ] **Step 4: Implement strategy bridge**

Write `battlesnake/c-core/server/battlesnake_strategy.c`:

```c
#include "battlesnake_strategy.h"

#include "../core/core_algorithms.h"

#include <string.h>

BsStrategyConfig BsStrategyConfigDefault(void) {
    BsStrategyConfig config;
    config.default_time_budget_ms = 400;
    return config;
}

static BsStrategyStatus fallback_move(const Board* board, const char* snake_id, MoveDirection* out_move) {
    MoveDirection moves[4];
    int count = BoardSafeMoves(board, snake_id, moves);
    if (count <= 0) {
        *out_move = MOVE_UP;
    } else {
        *out_move = moves[0];
    }
    return BS_STRATEGY_FALLBACK_USED;
}

BsStrategyStatus BsChooseMove(const Board* board, const char* snake_id, const BsStrategyConfig* config, MoveDirection* out_move) {
    if (board == 0 || snake_id == 0 || out_move == 0 || BoardFindSnakeConst(board, snake_id) == 0) {
        return BS_STRATEGY_ERROR;
    }

    int budget = 400;
    if (config != 0 && config->default_time_budget_ms > 0) {
        budget = config->default_time_budget_ms;
    }

    if (board->ruleset_name != 0 && strcmp(board->ruleset_name, "solo") == 0 && board->snake_count == 2) {
        CoreStatus status = CoreMinimaxMove(board, snake_id, budget, out_move);
        if (status == CORE_OK && *out_move != MOVE_INVALID) {
            return BS_STRATEGY_OK;
        }
        return fallback_move(board, snake_id, out_move);
    }

    return fallback_move(board, snake_id, out_move);
}
```

- [ ] **Step 5: Run strategy tests**

Run:

```bash
bash tools/run_c_server_tests.sh
```

Expected: PASS with exit code 0.

- [ ] **Step 6: Commit**

```bash
git add battlesnake/c-core/server/battlesnake_strategy.h battlesnake/c-core/server/battlesnake_strategy.c tests/c/test_battlesnake_strategy.c tools/run_c_server_tests.sh
git commit -m "feat: route native requests to battlesnake strategy"
```

## Task 5: HTTP Parser and Route Dispatcher

**Files:**
- Create: `battlesnake/c-core/server/battlesnake_http.h`
- Create: `battlesnake/c-core/server/battlesnake_http.c`
- Create: `tests/c/test_battlesnake_http.c`
- Modify: `tools/run_c_server_tests.sh`
- Test: `tests/c/test_battlesnake_http.c`

- [ ] **Step 1: Define HTTP API**

Create `battlesnake/c-core/server/battlesnake_http.h`:

```c
#pragma once

#include "arena.h"
#include "battlesnake_strategy.h"

#include <stddef.h>

typedef struct {
    int status_code;
    size_t response_len;
} BsHttpResult;

BsHttpResult BsHandleHttpRequest(
    const char* request,
    size_t request_len,
    BsArena* arena,
    const BsStrategyConfig* strategy_config,
    char* response,
    size_t response_capacity
);
```

- [ ] **Step 2: Write route tests**

Create `tests/c/test_battlesnake_http.c`:

```c
#include "../../battlesnake/c-core/server/battlesnake_http.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char* MOVE_BODY =
    "{\"game\":{\"id\":\"g\",\"ruleset\":{\"name\":\"standard\",\"settings\":{\"hazardDamagePerTurn\":0}},\"timeout\":500},"
    "\"turn\":1,"
    "\"board\":{\"height\":5,\"width\":5,\"snakes\":[{\"id\":\"me\",\"health\":90,\"body\":[{\"x\":2,\"y\":2},{\"x\":2,\"y\":1},{\"x\":2,\"y\":0}],\"length\":3}]},"
    "\"you\":{\"id\":\"me\",\"health\":90,\"body\":[{\"x\":2,\"y\":2},{\"x\":2,\"y\":1},{\"x\":2,\"y\":0}],\"length\":3}}";

static void request_from_body(char* out, size_t out_size, const char* path, const char* body) {
    int written = snprintf(out, out_size,
        "POST %s HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/json\r\nContent-Length: %zu\r\n\r\n%s",
        path,
        strlen(body),
        body);
    assert(written > 0);
    assert((size_t)written < out_size);
}

static void test_info_route(void) {
    BsArena arena;
    BsStrategyConfig config = BsStrategyConfigDefault();
    char response[2048];
    const char* request = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";
    assert(BsArenaInit(&arena, 65536));
    BsHttpResult result = BsHandleHttpRequest(request, strlen(request), &arena, &config, response, sizeof(response));
    assert(result.status_code == 200);
    assert(strstr(response, "HTTP/1.1 200 OK") != 0);
    assert(strstr(response, "\"apiversion\":\"1\"") != 0);
    BsArenaFree(&arena);
}

static void test_move_route(void) {
    BsArena arena;
    BsStrategyConfig config = BsStrategyConfigDefault();
    char request[4096];
    char response[2048];
    assert(BsArenaInit(&arena, 65536));
    request_from_body(request, sizeof(request), "/move", MOVE_BODY);
    BsHttpResult result = BsHandleHttpRequest(request, strlen(request), &arena, &config, response, sizeof(response));
    assert(result.status_code == 200);
    assert(strstr(response, "\"move\":\"") != 0);
    BsArenaFree(&arena);
}

static void test_unknown_route(void) {
    BsArena arena;
    BsStrategyConfig config = BsStrategyConfigDefault();
    char response[2048];
    const char* request = "GET /bad HTTP/1.1\r\nHost: x\r\n\r\n";
    assert(BsArenaInit(&arena, 65536));
    BsHttpResult result = BsHandleHttpRequest(request, strlen(request), &arena, &config, response, sizeof(response));
    assert(result.status_code == 404);
    BsArenaFree(&arena);
}

static void test_post_without_content_length(void) {
    BsArena arena;
    BsStrategyConfig config = BsStrategyConfigDefault();
    char response[2048];
    const char* request = "POST /move HTTP/1.1\r\nHost: x\r\n\r\n{}";
    assert(BsArenaInit(&arena, 65536));
    BsHttpResult result = BsHandleHttpRequest(request, strlen(request), &arena, &config, response, sizeof(response));
    assert(result.status_code == 400);
    BsArenaFree(&arena);
}

int main(void) {
    test_info_route();
    test_move_route();
    test_unknown_route();
    test_post_without_content_length();
    return 0;
}
```

- [ ] **Step 3: Wire HTTP tests**

Extend `tools/run_c_server_tests.sh` with a `gcc` command for `tests/c/test_battlesnake_http.c` that links:

```text
battlesnake/c-core/server/arena.c
battlesnake/c-core/server/battlesnake_json.c
battlesnake/c-core/server/battlesnake_strategy.c
battlesnake/c-core/server/battlesnake_http.c
battlesnake/c-core/datatypes/coord.c
battlesnake/c-core/datatypes/snake.c
battlesnake/c-core/datatypes/board.c
battlesnake/c-core/core/core_algorithms.c
battlesnake/c-core/core/position_eval.c
battlesnake/c-core/core/search_stats.c
battlesnake/c-core/core/search_workspace.c
battlesnake/c-core/core/search_state.c
battlesnake/c-core/core/zobrist.c
battlesnake/c-core/core/transposition_table.c
```

Run the resulting binary as `build/tests/test_battlesnake_http`.

- [ ] **Step 4: Implement dispatcher behavior**

Implement `battlesnake/c-core/server/battlesnake_http.c` with these route responses:

```c
GET / -> HTTP 200 body {"apiversion":"1","author":"codex","color":"#2563eb","head":"default","tail":"default","version":"0.1.0-native"}
POST /start -> HTTP 200 body {}
POST /move -> HTTP 200 body {"move":"<move>"}
POST /end -> HTTP 200 body {}
```

Header requirements:

- Always write `Content-Type: application/json`.
- Always write `Connection: close`.
- Always write exact `Content-Length`.
- Accept lowercase or mixed-case `content-length`.
- Parse only the first request in the buffer.
- Locate the body with `\r\n\r\n`.
- Compare method and path exactly.

- [ ] **Step 5: Run HTTP tests**

Run:

```bash
bash tools/run_c_server_tests.sh
```

Expected: PASS with exit code 0.

- [ ] **Step 6: Commit**

```bash
git add battlesnake/c-core/server/battlesnake_http.h battlesnake/c-core/server/battlesnake_http.c tests/c/test_battlesnake_http.c tools/run_c_server_tests.sh
git commit -m "feat: dispatch battlesnake http routes in C"
```

## Task 6: POSIX Socket Server

**Files:**
- Modify: `battlesnake/c-core/server/server_main.c`
- Modify: `tools/build_native_server.sh`
- Test: native server smoke with `curl`

- [ ] **Step 1: Replace skeleton main with socket server**

Implement `battlesnake/c-core/server/server_main.c` with:

```c
typedef struct {
    int port;
    size_t arena_bytes;
    size_t request_bytes;
    size_t response_bytes;
    BsStrategyConfig strategy;
} BsServerConfig;
```

Environment variables:

```text
BATTLESNAKE_PORT default 8000
BATTLESNAKE_ARENA_BYTES default 262144
BATTLESNAKE_MAX_REQUEST_BYTES default 196608
BATTLESNAKE_RESPONSE_BYTES default 4096
BATTLESNAKE_SEARCH_BUDGET_MS default 400
```

Runtime behavior:

- Create IPv4 TCP socket.
- Set `SO_REUSEADDR`.
- Bind to `0.0.0.0:BATTLESNAKE_PORT`.
- Listen backlog `128`.
- Accept one connection at a time for the first version.
- Read until `\r\n\r\n`, parse `Content-Length`, then read the full body.
- Call `BsHandleHttpRequest`.
- Write the full response with retry on short writes.
- Close the connection.
- Exit cleanly on `SIGINT` and `SIGTERM`.

- [ ] **Step 2: Build the server**

Run:

```bash
bash tools/build_native_server.sh
```

Expected: PASS and creates `build/battlesnake-server`.

- [ ] **Step 3: Start server for smoke test**

Run:

```bash
BATTLESNAKE_PORT=8090 BATTLESNAKE_SEARCH_BUDGET_MS=25 build/battlesnake-server
```

Expected stdout line:

```text
battlesnake native server listening on 0.0.0.0:8090
```

- [ ] **Step 4: Verify `GET /`**

In another shell, run:

```bash
curl -sS http://127.0.0.1:8090/
```

Expected:

```json
{"apiversion":"1","author":"codex","color":"#2563eb","head":"default","tail":"default","version":"0.1.0-native"}
```

- [ ] **Step 5: Verify `POST /move`**

Run this compact one-snake payload:

```bash
curl -sS -X POST http://127.0.0.1:8090/move -H 'Content-Type: application/json' --data '{"game":{"id":"g","ruleset":{"name":"standard","settings":{"hazardDamagePerTurn":0}},"timeout":500},"turn":1,"board":{"height":5,"width":5,"snakes":[{"id":"me","health":90,"body":[{"x":2,"y":2},{"x":2,"y":1},{"x":2,"y":0}],"length":3}]},"you":{"id":"me","health":90,"body":[{"x":2,"y":2},{"x":2,"y":1},{"x":2,"y":0}],"length":3}}'
```

Expected: HTTP 200 body with one valid move:

```json
{"move":"up"}
```

The exact move can be `up`, `down`, `left`, or `right`; the smoke test must assert membership in that set.

- [ ] **Step 6: Stop server**

Send `Ctrl-C`.

Expected stdout line:

```text
battlesnake native server stopped
```

- [ ] **Step 7: Commit**

```bash
git add battlesnake/c-core/server/server_main.c tools/build_native_server.sh
git commit -m "feat: serve battlesnake routes from native socket server"
```

## Task 7: Payload Fixtures and Equivalence Tests

**Files:**
- Create: `benchmarks/battlesnake_payloads.py`
- Create: `tests/test_native_server_equivalence.py`
- Test: Python unit tests plus native server smoke

- [ ] **Step 1: Create JSON payload generator**

Write `benchmarks/battlesnake_payloads.py`:

```python
from __future__ import annotations

import json
from typing import Any

from benchmarks.scenarios import SCENARIOS, Scenario


def coord_json(coord: Any) -> dict[str, int]:
    return {"x": int(coord.x), "y": int(coord.y)}


def snake_json(snake: Any) -> dict[str, Any]:
    body = [coord_json(coord) for coord in snake.body]
    return {
        "id": snake.id,
        "name": snake.name,
        "health": snake.health,
        "body": body,
        "latency": "0",
        "head": body[0],
        "length": snake.length,
        "shout": "",
        "customizations": {"color": "#2563eb", "head": "default", "tail": "default"},
    }


def move_payload(scenario: Scenario, turn: int = 14, timeout: int = 500) -> str:
    snakes = [snake_json(snake) for snake in scenario.snakes]
    you = next(snake for snake in snakes if snake["id"] == scenario.snake_id)
    payload = {
        "game": {
            "id": f"bench-{scenario.name}",
            "ruleset": {
                "name": scenario.ruleset_name,
                "version": "v1",
                "settings": {
                    "foodSpawnChance": 15,
                    "minimumFood": 1,
                    "hazardDamagePerTurn": scenario.hazard_damage,
                    "royale": {"shrinkEveryNTurns": 5},
                    "squad": {
                        "allowBodyCollisions": False,
                        "sharedElimination": False,
                        "sharedHealth": False,
                        "sharedLength": False,
                    },
                },
            },
            "map": "standard",
            "source": "custom",
            "timeout": timeout,
        },
        "turn": turn,
        "board": {
            "height": scenario.height,
            "width": scenario.width,
            "food": [coord_json(coord) for coord in scenario.food],
            "hazards": [coord_json(coord) for coord in scenario.hazards],
            "snakes": snakes,
        },
        "you": you,
    }
    return json.dumps(payload, separators=(",", ":"), sort_keys=True)


def payload_by_name(name: str) -> str:
    for scenario in SCENARIOS:
        if scenario.name == name:
            return move_payload(scenario)
    raise KeyError(f"unknown scenario: {name}")
```

- [ ] **Step 2: Write equivalence tests**

Create `tests/test_native_server_equivalence.py`:

```python
from __future__ import annotations

import json
import os
import socket
import subprocess
import time
import unittest

from benchmarks.battlesnake_payloads import payload_by_name


def wait_for_port(port: int) -> None:
    deadline = time.time() + 5.0
    while time.time() < deadline:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                return
        except OSError:
            time.sleep(0.05)
    raise RuntimeError(f"server did not listen on {port}")


def post_move(port: int, body: str) -> dict[str, str]:
    request = (
        f"POST /move HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\n"
        f"Content-Length: {len(body.encode('utf-8'))}\r\n\r\n{body}"
    ).encode("utf-8")
    with socket.create_connection(("127.0.0.1", port), timeout=2.0) as sock:
        sock.sendall(request)
        response = sock.recv(4096)
    header, payload = response.split(b"\r\n\r\n", 1)
    assert b"HTTP/1.1 200 OK" in header
    return json.loads(payload.decode("utf-8"))


class NativeServerEquivalenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        subprocess.run(["bash", "tools/build_native_server.sh"], check=True)

    def test_native_server_returns_valid_moves_for_existing_scenarios(self) -> None:
        port = 8091
        proc = subprocess.Popen(
            ["build/battlesnake-server"],
            env={**os.environ, "BATTLESNAKE_PORT": str(port), "BATTLESNAKE_SEARCH_BUDGET_MS": "25"},
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        try:
            wait_for_port(port)
            for name in ("duel_open_7x7", "duel_center_pressure_11x11", "standard_four_snakes_dense"):
                with self.subTest(name=name):
                    response = post_move(port, payload_by_name(name))
                    self.assertIn(response["move"], {"up", "down", "left", "right"})
        finally:
            proc.terminate()
            proc.wait(timeout=5)
```

- [ ] **Step 3: Run equivalence tests**

Run:

```bash
python3 -m unittest tests.test_native_server_equivalence -v
```

Expected: PASS.

- [ ] **Step 4: Commit**

```bash
git add benchmarks/battlesnake_payloads.py tests/test_native_server_equivalence.py
git commit -m "test: verify native server handles benchmark payloads"
```

## Task 8: HTTP Runtime Performance Benchmark

**Files:**
- Create: `benchmarks/bench_http_runtime.py`
- Test: benchmark output JSONL

- [ ] **Step 1: Write benchmark script**

Create `benchmarks/bench_http_runtime.py`:

```python
from __future__ import annotations

import argparse
import json
import os
import socket
import statistics
import subprocess
import sys
import time
from pathlib import Path

from benchmarks.battlesnake_payloads import payload_by_name


def rss_kb(pid: int) -> int:
    with open(f"/proc/{pid}/status", "r", encoding="utf-8") as fh:
        for line in fh:
            if line.startswith("VmRSS:"):
                return int(line.split()[1])
    return 0


def wait_for_port(port: int) -> None:
    deadline = time.time() + 10.0
    while time.time() < deadline:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                return
        except OSError:
            time.sleep(0.05)
    raise RuntimeError(f"server did not listen on {port}")


def request_once(port: int, method: str, path: str, body: str = "") -> float:
    body_bytes = body.encode("utf-8")
    request = (
        f"{method} {path} HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\nContent-Length: {len(body_bytes)}\r\n\r\n"
    ).encode("utf-8") + body_bytes
    started = time.perf_counter()
    with socket.create_connection(("127.0.0.1", port), timeout=5.0) as sock:
        sock.sendall(request)
        chunks: list[bytes] = []
        while True:
            chunk = sock.recv(4096)
            if not chunk:
                break
            chunks.append(chunk)
    elapsed_ms = (time.perf_counter() - started) * 1000.0
    response = b"".join(chunks)
    if b"HTTP/1.1 200 OK" not in response and b"HTTP/1.0 200 OK" not in response:
        raise RuntimeError(response[:200].decode("utf-8", errors="replace"))
    return elapsed_ms


def summarize(values: list[float]) -> dict[str, float]:
    ordered = sorted(values)
    return {
        "min_ms": ordered[0],
        "p50_ms": statistics.median(ordered),
        "p95_ms": ordered[int(round((len(ordered) - 1) * 0.95))],
        "max_ms": ordered[-1],
    }


def run_case(server: str, command: list[str], env: dict[str, str], port: int, path: str, method: str, body: str, runs: int, warmup: int) -> dict[str, object]:
    proc = subprocess.Popen(command, env={**os.environ, **env}, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    try:
        wait_for_port(port)
        for _ in range(warmup):
            request_once(port, method, path, body)
        values = [request_once(port, method, path, body) for _ in range(runs)]
        return {"server": server, "path": path, "runs": runs, "rss_kb": rss_kb(proc.pid), **summarize(values)}
    finally:
        proc.terminate()
        proc.wait(timeout=10)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--runs", type=int, default=100)
    parser.add_argument("--warmup", type=int, default=10)
    parser.add_argument("--out", type=Path, default=Path("benchmarks/results/http-runtime-baseline.jsonl"))
    args = parser.parse_args()
    payload = payload_by_name("duel_open_7x7")
    args.out.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(["bash", "tools/build_native_server.sh"], check=True)
    rows = [
        run_case("native", ["build/battlesnake-server"], {"BATTLESNAKE_PORT": "8092", "BATTLESNAKE_SEARCH_BUDGET_MS": "25"}, 8092, "/", "GET", "", args.runs, args.warmup),
        run_case("native", ["build/battlesnake-server"], {"BATTLESNAKE_PORT": "8092", "BATTLESNAKE_SEARCH_BUDGET_MS": "25"}, 8092, "/move", "POST", payload, args.runs, args.warmup),
        run_case("fastapi", [sys.executable, "-m", "uvicorn", "battlesnake.main:app", "--host", "127.0.0.1", "--port", "8093"], {}, 8093, "/", "GET", "", args.runs, args.warmup),
        run_case("fastapi", [sys.executable, "-m", "uvicorn", "battlesnake.main:app", "--host", "127.0.0.1", "--port", "8093"], {}, 8093, "/move", "POST", payload, args.runs, args.warmup),
    ]
    with args.out.open("w", encoding="utf-8") as fh:
        for row in rows:
            line = json.dumps(row, sort_keys=True)
            print(line)
            fh.write(line + "\n")
    native_move = next(row for row in rows if row["server"] == "native" and row["path"] == "/move")
    fastapi_move = next(row for row in rows if row["server"] == "fastapi" and row["path"] == "/move")
    if native_move["p95_ms"] >= fastapi_move["p95_ms"]:
        raise SystemExit("native /move p95 must be lower than fastapi /move p95")
    if native_move["rss_kb"] >= fastapi_move["rss_kb"]:
        raise SystemExit("native RSS must be lower than fastapi RSS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

- [ ] **Step 2: Run benchmark**

Run:

```bash
python3 benchmarks/bench_http_runtime.py --runs 100 --warmup 10 --out benchmarks/results/http-runtime-baseline.jsonl
```

Expected:

- Four JSONL rows are written.
- Native `/move` p95 is lower than FastAPI `/move` p95.
- Native RSS is lower than FastAPI RSS.

- [ ] **Step 3: Commit**

```bash
git add benchmarks/bench_http_runtime.py benchmarks/results/http-runtime-baseline.jsonl
git commit -m "bench: compare native and fastapi http runtime"
```

## Task 9: Runtime Migration

**Files:**
- Modify: `battlesnake/Dockerfile`
- Create: `requirements-dev.txt`
- Modify: `battlesnake/requirements.txt`
- Modify: `setup.py`
- Modify: `README.md`
- Modify: `docs/runbooks/battlesnake-deploy.md`
- Test: Docker build or local native run

- [ ] **Step 1: Replace Docker runtime with native executable**

Write `battlesnake/Dockerfile`:

```dockerfile
FROM gcc:14-bookworm AS build

WORKDIR /app
COPY . /app
RUN bash tools/build_native_server.sh

FROM debian:bookworm-slim

ENV BATTLESNAKE_PORT=8000
ENV BATTLESNAKE_SEARCH_BUDGET_MS=400

WORKDIR /app
COPY --from=build /app/build/battlesnake-server /app/battlesnake-server

EXPOSE 8000

CMD ["/app/battlesnake-server"]
```

- [ ] **Step 2: Move Python HTTP dependencies to development-only requirements**

Write `requirements-dev.txt`:

```text
fastapi
uvicorn
pydantic
```

Replace `battlesnake/requirements.txt` with:

```text
# Production runtime has no Python package dependencies.
```

Modify `setup.py` so package installation no longer pulls the general HTTP stack:

```python
setup(
    name="playing-battlesnake",
    version="0.1.0",
    packages=["battlesnake", "battlesnake.core", "battlesnake.strategies", "battlesnake.training"],
    package_data={"battlesnake": ["*.pyi", "py.typed"]},
    install_requires=[],
    python_requires=">=3.11",
    ext_modules=ext_modules,
    cmdclass={"build_ext": BuildExt},
)
```

The reference FastAPI app remains available only when `requirements-dev.txt` is installed for comparison benchmarks.

- [ ] **Step 3: Update README**

Replace `README.md` with:

````markdown
# playing-battlesnake

Native Battlesnake server with a Python development harness.

## Native build

```bash
bash tools/build_native_server.sh
```

## Native run

```bash
BATTLESNAKE_PORT=8000 BATTLESNAKE_SEARCH_BUDGET_MS=400 build/battlesnake-server
```

## Tests

```bash
python3 setup.py build_ext --inplace --force
python3 -m unittest discover -v
bash tools/run_c_server_tests.sh
python3 -m unittest tests.test_native_server_equivalence -v
```

## HTTP benchmark

```bash
python3 benchmarks/bench_http_runtime.py --runs 100 --warmup 10 --out benchmarks/results/http-runtime-baseline.jsonl
```
````

- [ ] **Step 4: Update deploy runbook**

Replace the Uvicorn command in `docs/runbooks/battlesnake-deploy.md` with:

````markdown
## Native Runtime

The deployed container runs `/app/battlesnake-server`.

Required environment:

```text
BATTLESNAKE_PORT=8000
BATTLESNAKE_SEARCH_BUDGET_MS=400
```

Health check:

```bash
curl -sS http://127.0.0.1:8000/
```

Expected response:

```json
{"apiversion":"1","author":"codex","color":"#2563eb","head":"default","tail":"default","version":"0.1.0-native"}
```
````

- [ ] **Step 5: Run full verification**

Run:

```bash
python3 setup.py build_ext --inplace --force
python3 -m pip install -r requirements-dev.txt
python3 -m unittest discover -v
bash tools/run_c_server_tests.sh
python3 -m unittest tests.test_native_server_equivalence -v
python3 benchmarks/bench_http_runtime.py --runs 100 --warmup 10 --out benchmarks/results/http-runtime-baseline.jsonl
```

Expected: all commands pass.

- [ ] **Step 6: Commit**

```bash
git add battlesnake/Dockerfile battlesnake/requirements.txt requirements-dev.txt setup.py README.md docs/runbooks/battlesnake-deploy.md benchmarks/results/http-runtime-baseline.jsonl
git commit -m "chore: migrate runtime to native battlesnake server"
```

## Task 10: Final Cleanup and Removal Gate

**Files:**
- Modify: `README.md`
- Test: full verification commands

- [ ] **Step 1: Decide whether to keep FastAPI app as development comparator**

Keep these files for one release after the native migration:

```text
main.py
battlesnake/main.py
battlesnake/types.py
battlesnake/game.py
```

Reason: `benchmarks/bench_http_runtime.py` uses them for direct runtime comparison, and retaining a known-good reference reduces migration risk.

- [ ] **Step 2: Mark native server as production runtime in README**

Append to `README.md`:

```markdown

## Runtime Ownership

Production runtime is the native C executable built by `tools/build_native_server.sh`.
The FastAPI app remains a development comparator for benchmarks and payload behavior checks.
```

- [ ] **Step 3: Run final verification**

Run:

```bash
git status --short
python3 setup.py build_ext --inplace --force
python3 -m unittest discover -v
bash tools/run_c_server_tests.sh
python3 -m unittest tests.test_native_server_equivalence -v
python3 benchmarks/bench_http_runtime.py --runs 100 --warmup 10 --out benchmarks/results/http-runtime-baseline.jsonl
```

Expected:

- Worktree shows only intentional files changed before commit.
- Python extension builds.
- Python tests pass.
- C tests pass.
- Native server equivalence tests pass.
- Runtime benchmark proves native `/move` p95 and RSS are lower than FastAPI.

- [ ] **Step 4: Commit**

```bash
git add README.md benchmarks/results/http-runtime-baseline.jsonl
git commit -m "docs: declare native server as production runtime"
```

## Acceptance Criteria

- Native executable builds with `bash tools/build_native_server.sh`.
- `GET /`, `POST /start`, `POST /move`, and `POST /end` are implemented without FastAPI, Uvicorn, Pydantic, or a general HTTP library in the runtime path.
- `/move` returns only `up`, `down`, `left`, or `right`.
- Native parser accepts the current Battlesnake request object shape including `game`, `turn`, `board`, `you`, board food, hazards, snakes, ruleset settings, and snake body arrays.
- Unknown JSON fields are ignored.
- Malformed JSON, missing `Content-Length`, oversized body, unknown route, and wrong method return deterministic HTTP status codes.
- Existing Python tests still pass.
- New C parser, strategy, and HTTP tests pass.
- Native server equivalence test passes for existing benchmark scenarios.
- Benchmark artifact `benchmarks/results/http-runtime-baseline.jsonl` shows native `/move` p95 lower than FastAPI `/move` p95 and native RSS lower than FastAPI RSS.
- Docker runtime starts the native executable directly.

## Self-Review

- Spec coverage: The plan covers current code inspection, Battlesnake docs routes, custom C HTTP server implementation, direct C strategy integration, performance tests, and migration to native runtime.
- Placeholder scan: No task depends on a missing future decision. Every new public function name and command is defined in the plan.
- Type consistency: `BsArena`, `BsGameRequest`, `BsStrategyConfig`, `MoveDirection`, and `BsHttpResult` are used consistently across tests and implementation tasks.
