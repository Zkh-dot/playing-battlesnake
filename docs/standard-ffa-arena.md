# Standard FFA Arena Harness

Run a paired local comparison between `standard-v1` and `first-safe`:

```bash
python3 tools/standard_ffa_arena.py \
  --games 20 \
  --seed 0 \
  --output benchmarks/results/standard-ffa-arena.json \
  --summary-output benchmarks/results/standard-ffa-arena.txt
```

The harness uses the native `Board.clone_and_apply` rules and the same Python
strategy interfaces as the dev snake. It runs candidate and baseline over the
same seeds to reduce variance.

The JSON report includes:

- placement distribution;
- placement score;
- death-cause counts;
- latency p50/p95/p99 and timeout rate;
- objective score;
- approximate 95% confidence intervals;
- paired placement/objective deltas.

The local environment used for issue #21 did not have the official
`battlesnake` CLI installed, so this runner is the reproducible local harness.
The report contract is intentionally machine-readable so a future CLI-backed
runner can emit the same shape.
