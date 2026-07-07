# Issue #22: Standard FFA Weight Tuning

## Scope

Add the rerunnable Standard FFA theta-search plumbing against the #21 arena objective. Do not claim the issue complete until a compute-node search produces a committed best-weights JSON that beats the hand-set seed on held-out arena seeds.

## Files

- Add `tools/tuning/search_standard_ffa_weights.py`.
- Update `tools/standard_ffa_arena.py` to accept `--candidate-theta`.
- Add `tests/test_issue_22_standard_ffa_weight_search.py`.
- Add `configs/evaluation_weights/standard-ffa-v1-tuned.json` after a successful search.
- Update docs with the rerunnable search and validation commands.

## Current Findings

- Compute-node transfer failed from this workstation:
  - direct `rsync` to `scv@192.168.1.6` timed out on SSH;
  - jump-host probe through `ya.sergeiscv.ru` also timed out on SSH to `192.168.1.6`.
- Local multi-seed mutation search command:
  - `python3 -m tools.tuning.search_standard_ffa_weights --search-mode mutate --mutation-scale 0.16 --trials 220 --games 4 --max-turns 80 --seed 20260707 --train-seeds 7000,9000,11000,13000,15000 --output configs/evaluation_weights/standard-ffa-v1-tuned.json --trials-output /tmp/standard-ffa-v1-multiseed-trials.jsonl`
- Local search best score:
  - `0.88759375`
- Held-out arena, tuned file (`seed=17000`, 16 games):
  - objective `0.7111`, placement score `0.5750`, latency p95 `0.854 ms`, gate passed;
  - placements `{'1': 4, '2': 8, '3': 4, '4': 0}`.
- Held-out arena, hand-set theta (`seed=17000`, 16 games):
  - objective `0.6141`, placement score `0.4750`, latency p95 `0.855 ms`, gate passed;
  - placements `{'1': 2, '2': 8, '3': 6, '4': 0}`.

## Next Step

Commit the tuned config as a local held-out winner, then rerun on the compute node when SSH access is available if strict issue acceptance requires actual node provenance.

## Verification

- `python3 -m pytest tests/test_issue_22_standard_ffa_weight_search.py`
- `python3 tools/standard_ffa_arena.py --games 16 --max-turns 80 --seed 17000 --candidate-theta configs/evaluation_weights/standard-ffa-v1-tuned.json --output /tmp/tuned-file-heldout-16.json --summary-output /tmp/tuned-file-heldout-16.txt`
- `python3 tools/standard_ffa_arena.py --games 16 --max-turns 80 --seed 17000 --output /tmp/default-file-heldout-16.json --summary-output /tmp/default-file-heldout-16.txt`
