# Issue #21: Standard FFA Arena Harness

## Scope

Add a reproducible paired arena harness for Standard FFA strategy changes. The local environment does not have the official `battlesnake` CLI installed, so this issue lands a deterministic local runner over the same native `Board.clone_and_apply` rules and strategy interfaces. The command shape keeps candidate/baseline/opponent participants explicit so a CLI-backed runner can be added without changing the report contract.

## Files

- Add `tools/standard_ffa_arena.py`.
- Add `tests/test_issue_21_standard_ffa_arena.py`.
- Add `docs/standard-ffa-arena.md`.
- Update `.gitignore` for generated Standard FFA arena reports.

## Harness Contract

- One command:
  - runs paired candidate vs baseline games over the same seeds;
  - writes machine-readable JSON;
  - optionally writes a human summary;
  - prints the summary to stdout.
- Metrics:
  - placement distribution;
  - placement score: `1.00*P[1st] + 0.55*P[2nd] + 0.20*P[3rd]`;
  - average turn score;
  - death-cause counts and weighted death penalty;
  - latency p50/p95/p99 and timeout rate;
  - objective: `w_place * placement_score + w_turns * turn_score - w_death * death_penalty - w_latency * timeout_rate`;
  - approximate 95% confidence interval for objective over paired seeds.
- Latency gate:
  - report `latency_gate_passed`;
  - return non-zero when candidate p95 exceeds the configured budget unless `--no-fail-on-latency` is set.

## Verification

- `python3 -m pytest tests/test_issue_21_standard_ffa_arena.py`
- Smoke command:
  - `python3 tools/standard_ffa_arena.py --games 4 --max-turns 40 --seed 123 --output /tmp/standard-ffa-arena.json --summary-output /tmp/standard-ffa-arena.txt`
