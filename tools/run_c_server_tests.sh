#!/usr/bin/env bash
set -euo pipefail

mkdir -p build/tests

extra_cflags=()
if [[ -n ${CFLAGS:-} ]]; then
  read -r -a extra_cflags <<< "${CFLAGS}"
fi

"${CC:-cc}" -std=c2x -D_POSIX_C_SOURCE=200809L "${extra_cflags[@]}" -Ibattlesnake/c-core \
  tests/c/test_active_connections.c \
  battlesnake/c-core/server/active_connections.c \
  -pthread \
  -o build/tests/test_active_connections

build/tests/test_active_connections

"${CC:-cc}" -std=c2x -D_POSIX_C_SOURCE=200809L "${extra_cflags[@]}" -Ibattlesnake/c-core \
  tests/c/test_connection_queue.c \
  battlesnake/c-core/server/connection_queue.c \
  -pthread \
  -o build/tests/test_connection_queue

build/tests/test_connection_queue

"${CC:-cc}" -std=c2x -D_POSIX_C_SOURCE=200809L "${extra_cflags[@]}" -Ibattlesnake/c-core \
  tests/c/test_overload_response.c \
  battlesnake/c-core/server/overload_response.c \
  -o build/tests/test_overload_response

build/tests/test_overload_response

"${CC:-cc}" -std=c2x -D_POSIX_C_SOURCE=200809L "${extra_cflags[@]}" -Ibattlesnake/c-core \
  tests/c/test_arena.c \
  battlesnake/c-core/server/arena.c \
  -o build/tests/test_arena

build/tests/test_arena

"${CC:-cc}" -std=c2x -D_POSIX_C_SOURCE=200809L "${extra_cflags[@]}" -Ibattlesnake/c-core \
  tests/c/test_duel_weight_profiles.c \
  battlesnake/c-core/core/duel_weight_profiles_generated.c \
  -o build/tests/test_duel_weight_profiles

build/tests/test_duel_weight_profiles

"${CC:-cc}" -std=c2x -D_POSIX_C_SOURCE=200809L "${extra_cflags[@]}" -Ibattlesnake/c-core \
  tests/c/test_battlesnake_json.c \
  battlesnake/c-core/server/arena.c \
  battlesnake/c-core/server/battlesnake_json.c \
  battlesnake/c-core/datatypes/coord.c \
  battlesnake/c-core/datatypes/snake.c \
  battlesnake/c-core/datatypes/board.c \
  -o build/tests/test_battlesnake_json

build/tests/test_battlesnake_json

"${CC:-cc}" -std=c2x -D_POSIX_C_SOURCE=200809L -DCORE_ROOT_SELECTION_TESTING "${extra_cflags[@]}" -Ibattlesnake/c-core \
  tests/c/test_battlesnake_strategy.c \
  battlesnake/c-core/server/battlesnake_strategy.c \
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
  -lm \
  -o build/tests/test_battlesnake_strategy

build/tests/test_battlesnake_strategy

"${CC:-cc}" -std=c2x -D_POSIX_C_SOURCE=200809L "${extra_cflags[@]}" -Ibattlesnake/c-core \
  tests/c/test_battlesnake_http.c \
  battlesnake/c-core/server/arena.c \
  battlesnake/c-core/server/battlesnake_json.c \
  battlesnake/c-core/server/battlesnake_strategy.c \
  battlesnake/c-core/server/battlesnake_http.c \
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
  -lm \
  -o build/tests/test_battlesnake_http

build/tests/test_battlesnake_http
