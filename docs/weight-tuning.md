# Replay Weight Tuning

This project tunes minimax evaluation weights from exported Battlesnake games in `exports/`.

The first tuning scope is opponent-pressure weights:

- `opponent_reachable_space`
- `territory_delta`
- `opponent_safe_moves`
- `opponent_low_health_food_denial`

## Objective

The objective is deterministic replay agreement under fixed-depth minimax:

```text
score = exact_match_rate - 0.10 * error_rate - 0.02 * timeout_rate
```

Use `fixed_depth=3` for real runs. Use lower depth and `--limit` only for smoke tests.

`tools.tuning.search_weights` uses Optuna TPE when `optuna` is installed. On a
clean local environment without Optuna, it automatically falls back to the
stdlib deterministic random search and still writes the requested best-weights
JSON.

The search CLI writes final weights to `--output`. Random-search trials are
logged next to that file as `<output-stem>-trials.jsonl`. Optuna runs emit one
JSON progress row per trial to stdout, so the remote runner captures progress in
`artifacts/weight_tuning/search.log`. Optuna search stops early after
`--plateau-patience` non-improving trials; set `--plateau-patience 0` to force
all requested trials.

## Local Smoke Test

```bash
python3 setup.py build_ext --inplace --force
python3 -B -m unittest tests.test_replay_dataset tests.test_evaluate_weights -v
python3 -B -m tools.tuning.evaluate_weights \
  --exports exports/andreammm_games \
  --weights configs/evaluation_weights/default.json \
  --split validation \
  --fixed-depth 1 \
  --time-budget-ms 1000 \
  --limit 20
```

## Compute Node Run

The compute node is:

```text
scv@192.168.1.6
```

Start a full run:

```bash
TRIALS=300 FIXED_DEPTH=3 TIME_BUDGET_MS=5000 tools/tuning/remote_weight_tuning.sh
```

Watch the run:

```bash
ssh scv@192.168.1.6 'tmux attach -t battlesnake-weight-tuning'
```

If `tmux` is unavailable on the compute node, the runner falls back to a
background process. Watch that run with:

```bash
ssh scv@192.168.1.6 'tail -f /tmp/playing-battlesnake-weight-tuning/artifacts/weight_tuning/search.log'
```

Fetch results:

```bash
rsync -az scv@192.168.1.6:/tmp/playing-battlesnake-weight-tuning/artifacts/weight_tuning/ artifacts/weight_tuning/
```

## Final Validation

Evaluate the best weights on all splits:

```bash
python3 -B -m tools.tuning.evaluate_weights \
  --exports exports \
  --weights artifacts/weight_tuning/best_weights.json \
  --split train \
  --fixed-depth 3 \
  --time-budget-ms 5000

python3 -B -m tools.tuning.evaluate_weights \
  --exports exports \
  --weights artifacts/weight_tuning/best_weights.json \
  --split validation \
  --fixed-depth 3 \
  --time-budget-ms 5000

python3 -B -m tools.tuning.evaluate_weights \
  --exports exports \
  --weights artifacts/weight_tuning/best_weights.json \
  --split test \
  --fixed-depth 3 \
  --time-budget-ms 5000
```

Promote a tuned config only if validation improves over `configs/evaluation_weights/default.json` and test does not regress materially.
