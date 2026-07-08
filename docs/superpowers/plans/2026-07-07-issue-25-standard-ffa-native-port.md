# Issue #25: Standard FFA native port

## Plan

1. Add a C `standard_ffa` core module for the frozen deterministic strategy.
2. Keep two-snake standard/solo routing on duel minimax.
3. Keep the production route conservative until native selected-move parity is
   broad enough for Standard FFA traffic.
4. Expose the C strategy through the Python extension for selected-move parity
   tests.
5. Add parity/routing tests and an arena option for native-vs-Python
   confirmation.
6. Update the deploy runbook with Standard FFA smoke and observation steps.

## Result

Implemented:

- `battlesnake/c-core/core/standard_ffa.c`
- `battlesnake/c-core/core/standard_ffa.h`
- `standard_ffa_move(...)` extension function
- conservative production routing in `battlesnake/c-core/server/battlesnake_strategy.c`
- `standard-v1-native` arena strategy option
- parity test corpus in `tests/test_issue_25_standard_ffa_native.py`
- C routing coverage in `tests/c/test_battlesnake_strategy.c`
- runbook and native port report docs

The native port uses deterministic/uniform priors. The LightGBM prior is not
ported because #23 was a no-go. The #24 guardrail is included conservatively
for forced solo/frozen-obstacle traps.

After review, standard 3+ snake production traffic remains on the prior
first-safe fallback instead of routing into the C scorer. The native scorer is
kept behind the Python extension and arena harness until a broader parity corpus
is green.

## Validation

Commands:

```bash
python3 setup.py build_ext --inplace --force
python3 -m pytest tests/test_issue_25_standard_ffa_native.py
python3 -m pytest tests/test_issue_21_standard_ffa_arena.py tests/test_issue_25_standard_ffa_native.py
tools/run_c_server_tests.sh
tools/build_native_server.sh
```

Arena:

- 24 paired games, seed 71000;
- native C objective 0.8311;
- Python Standard FFA objective 0.7979;
- native p95 latency 0.295 ms;
- Python p95 latency 60.072 ms;
- placement-score delta +0.0292 for native.

Detailed report: `docs/standard-ffa-native-port-report.md`.
