# Standard FFA V1 Smoke Results

Date: 2026-07-07

Branch: `standard-ffa-issues-17-25`

Purpose: issue #19 local smoke comparison and latency record for the Python
`StrategyStandard` dev-snake implementation.

Command:

```bash
python3 - <<'PY'
# Deterministic 20-seed local arena: snake-0 uses StrategyStandard or
# StrategyFirstSafe against first-safe opponents on identical seeds.
# Then measure 200 StrategyStandard.decide calls on a representative 4-snake board.
PY
```

Results:

```text
standard avg placement=1.80; first-safe avg placement=2.65
standard placements=[1, 1, 2, 1, 2, 2, 3, 1, 3, 1, 1, 2, 2, 2, 2, 3, 3, 1, 2, 1]
first_safe placements=[2, 2, 2, 3, 3, 3, 3, 2, 3, 3, 2, 3, 3, 3, 2, 2, 3, 3, 4, 2]
standard avg turns=105.3; first-safe avg turns=113.0
latency samples=200 mean_ms=0.676 p95_ms=0.707 max_ms=1.037
```

Interpretation:

- Lower placement is better; `standard-v1` beat first-safe in this smoke run.
- p95 local decision latency was well below the issue #19 80 ms target.
- This is a smoke check only; the formal arena harness remains follow-up work.
