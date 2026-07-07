# Issue #22: Standard FFA Weight Tuning

## Scope

Add the rerunnable Standard FFA theta-search plumbing against the #21 arena objective. Do not claim the issue complete until a compute-node search produces a committed best-weights JSON that beats the hand-set seed on held-out arena seeds.

## Files

- Add `tools/tuning/search_standard_ffa_weights.py`.
- Update `tools/standard_ffa_arena.py` to accept `--candidate-theta`.
- Add `tests/test_issue_22_standard_ffa_weight_search.py`.
- Later, after a successful search, add `configs/evaluation_weights/standard-ffa-v1-tuned.json` and update docs with the compute-node run.

## Current Local Findings

- Local search command tested:
  - `python3 -m tools.tuning.search_standard_ffa_weights --trials 30 --games 8 --max-turns 80 --seed 2200 --output configs/evaluation_weights/standard-ffa-v1-tuned.json --trials-output /tmp/standard-ffa-v1-tuning-trials.jsonl`
- Search-seed comparison (`seed=2200`, 8 games):
  - tuned objective `0.8270`, placement score `0.6875`;
  - hand-set objective `0.6714`, placement score `0.5312`.
- Fresh-seed comparison did not pass the acceptance bar:
  - `seed=9000`: tuned objective `0.8041`, hand-set objective `0.8512`;
  - focused 80-trial search on `seed=9000` found trial zero as best.

## Next Step

Run a larger compute-node search or revise the search space/objective before committing best weights. The acceptance criterion is not met until the resulting theta beats the hand-set seed on fresh held-out arena seeds.

## Verification

- `python3 -m pytest tests/test_issue_22_standard_ffa_weight_search.py`
