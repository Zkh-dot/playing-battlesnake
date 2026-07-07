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

## Standard FFA Arena Theta

Issue #22 tunes `StrategyStandard` theta values against the Standard FFA paired
arena objective from `tools/standard_ffa_arena.py`. The search starts from the
hand-set theta, mutates bounded parameters, prunes candidates that fail the
scenario suite or latency gate, and scores candidates by mean arena objective
across the configured training seeds.

Rerun the accepted local-workstation multi-seed search:

```bash
python3 -m tools.tuning.search_standard_ffa_weights \
  --search-mode mutate \
  --mutation-scale 0.12 \
  --trials 480 \
  --games 6 \
  --max-turns 80 \
  --seed 20260708 \
  --train-seeds 7000,9000,11000,13000,15000,19000 \
  --output /tmp/standard-ffa-v1-local-strong.json \
  --trials-output /tmp/standard-ffa-v1-local-strong-trials.jsonl

cp /tmp/standard-ffa-v1-local-strong.json \
  configs/evaluation_weights/standard-ffa-v1-tuned.json
```

The accepted local workstation run wrote `/tmp/standard-ffa-v1-local-strong.json`
with best score
`0.8115972222222223`. The compute node was unavailable during issue #22, and
the maintainer approved local workstation tuning as the slower fallback. After
held-out validation, that JSON was promoted to
`configs/evaluation_weights/standard-ffa-v1-tuned.json`.

Run the same Standard FFA search on the compute node:

```bash
TRIALS=220 \
GAMES=4 \
MAX_TURNS=80 \
SEED=20260707 \
TRAIN_SEEDS=7000,9000,11000,13000,15000 \
tools/tuning/remote_standard_ffa_weight_tuning.sh
```

If direct LAN SSH is unavailable but the reverse tunnel is listening on
`ro.sergeiscv.ru`, run the same command through the loopback tunnel:

```bash
REMOTE=scv@127.0.0.1 \
SSH_PROXY_JUMP=ro.sergeiscv.ru \
SSH_PORT=2206 \
TRIALS=220 \
GAMES=4 \
MAX_TURNS=80 \
SEED=20260707 \
TRAIN_SEEDS=7000,9000,11000,13000,15000 \
tools/tuning/remote_standard_ffa_weight_tuning.sh
```

Watch the run:

```bash
ssh scv@192.168.1.6 'tmux attach -t battlesnake-standard-ffa-tuning'
```

If `tmux` is unavailable on the compute node, watch the background run with:

```bash
ssh scv@192.168.1.6 'tail -f /tmp/playing-battlesnake-standard-ffa-tuning/artifacts/standard_ffa_weight_tuning/search.log'
```

Fetch remote artifacts:

```bash
rsync -az scv@192.168.1.6:/tmp/playing-battlesnake-standard-ffa-tuning/artifacts/standard_ffa_weight_tuning/ artifacts/standard_ffa_weight_tuning/
```

Validate the tuned file on held-out arena seeds:

```bash
python3 tools/standard_ffa_arena.py \
  --games 24 \
  --max-turns 80 \
  --seed 17000 \
  --candidate-theta configs/evaluation_weights/standard-ffa-v1-tuned.json \
  --output /tmp/new-heldout-17000.json \
  --summary-output /tmp/new-heldout-17000.txt
```

Compare the hand-set theta on the same held-out batch:

```bash
python3 tools/standard_ffa_arena.py \
  --games 24 \
  --max-turns 80 \
  --seed 17000 \
  --output /tmp/default-heldout-17000.json \
  --summary-output /tmp/default-heldout-17000.txt
```

Held-out results from the local run:

| seed | theta | objective | placement_score | latency_p95_ms | placements |
| ---: | --- | ---: | ---: | ---: | --- |
| `17000` | tuned file | `0.8382` | `0.7125` | `0.939` | `{'1': 11, '2': 10, '3': 3, '4': 0}` |
| `17000` | hand-set | `0.7142` | `0.5750` | `1.013` | `{'1': 6, '2': 12, '3': 6, '4': 0}` |
| `23000` | tuned file | `0.7007` | `0.5604` | `0.924` | `{'1': 6, '2': 11, '3': 7, '4': 0}` |
| `23000` | hand-set | `0.6938` | `0.5521` | `1.051` | `{'1': 4, '2': 15, '3': 5, '4': 0}` |
| `31000` | tuned file | `0.5901` | `0.4479` | `1.612` | `{'1': 2, '2': 13, '3': 8, '4': 1}` |
| `31000` | hand-set | `0.6263` | `0.4833` | `1.118` | `{'1': 2, '2': 16, '3': 4, '4': 2}` |

Across these three fresh 24-game held-out batches, average objective was
`0.7097` for the tuned file versus `0.6781` for the hand-set theta. The tuned
file also averaged above the previous committed local candidate (`0.7021` across
the same seeds), so it replaced the earlier local config.

The compute-node path for this repository is still `scv@192.168.1.6`, but the
issue #22 workstation attempt could not reach it: direct SSH timed out, and the
same target also timed out when probed through `ya.sergeiscv.ru`. Rerun the same
commands there when SSH access is restored if strict acceptance requires compute
node provenance.
