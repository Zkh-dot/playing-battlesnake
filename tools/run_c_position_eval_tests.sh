#!/usr/bin/env bash
set -euo pipefail

sources=(
  tests/c/test_position_eval.c
  battlesnake/c-core/datatypes/coord.c
  battlesnake/c-core/datatypes/snake.c
  battlesnake/c-core/datatypes/board.c
  battlesnake/c-core/core/search_stats.c
  battlesnake/c-core/core/core_algorithms.c
  battlesnake/c-core/core/search_workspace.c
  battlesnake/c-core/core/search_state.c
  battlesnake/c-core/core/zobrist.c
  battlesnake/c-core/core/transposition_table.c
  battlesnake/c-core/core/position_eval.c
)

gcc -std=c2x -D_POSIX_C_SOURCE=200809L -DCORE_POSITION_EVAL_TESTING \
  -I battlesnake/c-core \
  "${sources[@]}" \
  -lm \
  -o /tmp/test_position_eval

/tmp/test_position_eval

if gcc -std=c2x -fopenmp -x c -o /tmp/openmp_smoke - >/dev/null 2>&1 <<'EOF'
#include <omp.h>
int main(void) { return omp_get_max_threads() < 1; }
EOF
then
  gcc -std=c2x -Wall -Wextra -D_POSIX_C_SOURCE=200809L \
    -DCORE_POSITION_EVAL_TESTING -DCORE_POSITION_EVAL_OPENMP -fopenmp \
    -I battlesnake/c-core \
    "${sources[@]}" \
    -lm \
    -o /tmp/test_position_eval_openmp

  OMP_NUM_THREADS=1 /tmp/test_position_eval_openmp
  OMP_NUM_THREADS=2 /tmp/test_position_eval_openmp
  OMP_NUM_THREADS=4 /tmp/test_position_eval_openmp
else
  echo "OpenMP smoke compile failed; skipping OpenMP position_eval tests" >&2
fi
