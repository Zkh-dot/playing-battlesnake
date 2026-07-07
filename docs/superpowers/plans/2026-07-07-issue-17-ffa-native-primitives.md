# Issue #17: Standard FFA Native Primitive Regression Tests

## Scope

Add deterministic Python tests against `battlesnake.battlesnake_native` for the native primitives used by the Standard FFA scoring loop. Keep the change focused on issue #17. Do not change Standard FFA strategy selection or scoring code.

## Files

- Add `tests/test_issue_17_ffa_native_primitives.py`.
- Touch `battlesnake/c-core/datatypes/board.c` or `battlesnake/c-core/core/core_algorithms.c` only if the new tests expose a rules divergence.

## Test Coverage

- `Board.clone_and_apply`
  - equal-length head-to-head kills both snakes;
  - unequal-length head-to-head kills the shorter snake only;
  - body collision removes the colliding snake;
  - food consumption grows the snake, removes eaten food, and resets health;
  - ordinary movement decrements health and vacates the tail;
  - hazard movement applies hazard damage;
  - dead snakes are removed from the resulting board.
- `reachable_space`
  - document and pin current tail-aware semantics: non-constrictor tails that are expected to vacate are traversable, while an opponent tail remains blocked when that opponent can eat next turn.
- `voronoi_territory`
  - 3-4 snake FFA ownership;
  - tie cells are not assigned;
  - dead snakes do not receive territory;
  - empty boards return an empty result.
- `Board.safe_moves`
  - equal-or-longer opponent head-adjacent cells are unsafe;
  - shorter opponent head-adjacent cells remain legal;
  - vacating tails are legal destinations where applicable.

## Verification

- Build native extension if needed:
  - `python3 setup.py build_ext --inplace --force`
- Run targeted tests:
  - `python3 -m pytest tests/test_issue_17_ffa_native_primitives.py`
- Run broader relevant tests:
  - `python3 -m pytest tests/test_zobrist_hash.py tests/test_search_diagnostics.py tests/test_issue_17_ffa_native_primitives.py`
  - `tools/run_c_position_eval_tests.sh`
  - `tools/run_c_server_tests.sh`
