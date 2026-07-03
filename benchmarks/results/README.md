# Benchmark Results

Generated benchmark files in this directory are local performance artifacts. Keep the named JSONL files when working on search performance so comparisons stay reproducible:

- `baseline-before-tt.jsonl`
- `after-tt.jsonl`
- `after-ordering.jsonl`
- `after-workspace.jsonl`
- `after-makeunmake.jsonl`
- `final.jsonl`

Use the comparator from the repository root:

```bash
python3 -B -m benchmarks.compare_benchmarks benchmarks/results/baseline-before-tt.jsonl benchmarks/results/final.jsonl
```

Budget rows are gated on completed depth and elapsed p50. Baseline-matched fixed-depth rows are gated on move stability; fixed-depth elapsed p50 is printed for review because low-millisecond rows are sensitive to host scheduler noise. Extra candidate-only fixed-depth rows are retained as review evidence but are not compared by this baseline command.
