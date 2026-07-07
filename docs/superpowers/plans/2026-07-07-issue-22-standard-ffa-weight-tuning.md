# Issue #22: Standard FFA Weight Tuning

## Scope

Add the rerunnable Standard FFA theta-search plumbing against the #21 arena objective. The original issue requested compute-node tuning; after repeated compute-node and reverse-tunnel failures, the maintainer approved slower local-workstation tuning for this issue.

## Files

- Add `tools/tuning/search_standard_ffa_weights.py`.
- Update `tools/standard_ffa_arena.py` to accept `--candidate-theta`.
- Add `tests/test_issue_22_standard_ffa_weight_search.py`.
- Add `configs/evaluation_weights/standard-ffa-v1-tuned.json` after a successful search.
- Add `tools/tuning/remote_standard_ffa_weight_tuning.sh` for a compute-node Standard FFA run matching the local search command.
  - Support `SSH_PROXY_JUMP` and `SSH_PORT` so the same runner works through the `ro.sergeiscv.ru` reverse tunnel when it is listening.
- Update docs with the rerunnable search and validation commands.

## Current Findings

- Compute-node transfer failed from this workstation:
  - direct `rsync` to `scv@192.168.1.6` timed out on SSH;
  - jump-host probe through `ya.sergeiscv.ru` also timed out on SSH to `192.168.1.6`.
- The documented reverse tunnel through `ro.sergeiscv.ru:2206` was also unavailable.
- Local-workstation multi-seed mutation search command:
  - `python3 -m tools.tuning.search_standard_ffa_weights --search-mode mutate --mutation-scale 0.12 --trials 480 --games 6 --max-turns 80 --seed 20260708 --train-seeds 7000,9000,11000,13000,15000,19000 --output /tmp/standard-ffa-v1-local-strong.json --trials-output /tmp/standard-ffa-v1-local-strong-trials.jsonl`
- Compute-node command prepared:
  - `TRIALS=220 GAMES=4 MAX_TURNS=80 SEED=20260707 TRAIN_SEEDS=7000,9000,11000,13000,15000 tools/tuning/remote_standard_ffa_weight_tuning.sh`
  - Tunnel variant: `REMOTE=scv@127.0.0.1 SSH_PROXY_JUMP=ro.sergeiscv.ru SSH_PORT=2206 TRIALS=220 GAMES=4 MAX_TURNS=80 SEED=20260707 TRAIN_SEEDS=7000,9000,11000,13000,15000 tools/tuning/remote_standard_ffa_weight_tuning.sh`
- Local search best score:
  - `0.8115972222222223`
- Held-out arena averages across seeds `17000`, `23000`, and `31000`, 24 games each:
  - tuned file average objective `0.7097`;
  - previous committed local candidate average objective `0.7021`;
  - hand-set theta average objective `0.6781`.
- Per-seed tuned vs hand-set objective:
  - `17000`: tuned `0.8382`, hand-set `0.7142`;
  - `23000`: tuned `0.7007`, hand-set `0.6938`;
  - `31000`: tuned `0.5901`, hand-set `0.6263`.

## Next Step

Commit the final local-workstation tuned config and comment on #22 with the local fallback evidence.

## Verification

- `python3 -m pytest tests/test_issue_22_standard_ffa_weight_search.py`
- `python3 -m pytest tests/test_issue_21_standard_ffa_arena.py tests/test_issue_22_standard_ffa_weight_search.py`
- `bash -n tools/tuning/remote_standard_ffa_weight_tuning.sh`
- `python3 tools/standard_ffa_arena.py --games 24 --max-turns 80 --seed 17000 --candidate-theta /tmp/standard-ffa-v1-local-strong.json --output /tmp/new-heldout-17000.json --summary-output /tmp/new-heldout-17000.txt`
- `python3 tools/standard_ffa_arena.py --games 24 --max-turns 80 --seed 23000 --candidate-theta /tmp/standard-ffa-v1-local-strong.json --output /tmp/new-heldout-23000.json --summary-output /tmp/new-heldout-23000.txt`
- `python3 tools/standard_ffa_arena.py --games 24 --max-turns 80 --seed 31000 --candidate-theta /tmp/standard-ffa-v1-local-strong.json --output /tmp/new-heldout-31000.json --summary-output /tmp/new-heldout-31000.txt`
- Matching hand-set theta runs for seeds `17000`, `23000`, and `31000`.
