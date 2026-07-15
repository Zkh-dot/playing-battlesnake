#!/usr/bin/env bash
set -euo pipefail

mkdir -p build

extra_cflags=()
if [[ -n ${CFLAGS:-} ]]; then
  read -r -a extra_cflags <<< "${CFLAGS}"
fi

"${CC:-gcc}" \
  -std=c2x \
  -D_POSIX_C_SOURCE=200809L \
  -O3 \
  -DNDEBUG \
  "${extra_cflags[@]}" \
  -Ibattlesnake/c-core \
  battlesnake/c-core/datatypes/coord.c \
  battlesnake/c-core/datatypes/snake.c \
  battlesnake/c-core/datatypes/board.c \
  battlesnake/c-core/core/core_algorithms.c \
  battlesnake/c-core/core/standard_ffa.c \
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
  battlesnake/c-core/server/connection_queue.c \
  battlesnake/c-core/server/overload_response.c \
  battlesnake/c-core/server/server_main.c \
  -lm \
  -pthread \
  -o build/battlesnake-server
