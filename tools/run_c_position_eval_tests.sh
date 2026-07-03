#!/usr/bin/env bash
set -euo pipefail

gcc -std=c2x -D_POSIX_C_SOURCE=200809L -DCORE_POSITION_EVAL_TESTING \
  -I battlesnake/c-core \
  tests/c/test_position_eval.c \
  battlesnake/c-core/datatypes/coord.c \
  battlesnake/c-core/datatypes/snake.c \
  battlesnake/c-core/datatypes/board.c \
  battlesnake/c-core/core/search_stats.c \
  battlesnake/c-core/core/core_algorithms.c \
  battlesnake/c-core/core/search_workspace.c \
  battlesnake/c-core/core/search_state.c \
  battlesnake/c-core/core/zobrist.c \
  battlesnake/c-core/core/transposition_table.c \
  battlesnake/c-core/core/position_eval.c \
  -lm \
  -o /tmp/test_position_eval

/tmp/test_position_eval
