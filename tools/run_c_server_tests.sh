#!/usr/bin/env bash
set -euo pipefail

mkdir -p build/tests

"${CC:-cc}" -std=c2x -D_POSIX_C_SOURCE=200809L -Ibattlesnake/c-core \
  tests/c/test_arena.c \
  battlesnake/c-core/server/arena.c \
  -o build/tests/test_arena

build/tests/test_arena

"${CC:-cc}" -std=c2x -D_POSIX_C_SOURCE=200809L -Ibattlesnake/c-core \
  tests/c/test_battlesnake_json.c \
  battlesnake/c-core/server/arena.c \
  battlesnake/c-core/server/battlesnake_json.c \
  battlesnake/c-core/datatypes/coord.c \
  battlesnake/c-core/datatypes/snake.c \
  battlesnake/c-core/datatypes/board.c \
  -o build/tests/test_battlesnake_json

build/tests/test_battlesnake_json
