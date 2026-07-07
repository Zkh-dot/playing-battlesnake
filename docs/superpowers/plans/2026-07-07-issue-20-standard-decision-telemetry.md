# Issue #20: Standard FFA Decision Telemetry

## Scope

Add inspectable decision telemetry for the Python dev snake. Reuse issue #18 classification records and issue #19 scoring internals. Keep telemetry optional and low overhead.

## Files

- Update `battlesnake/strategies/standard.py` to expose a per-turn decision record.
- Add `battlesnake/decision_telemetry.py` for async JSONL writes and death-cause tagging.
- Update `battlesnake/main.py` to write move and end records.
- Add `tools/analyze_standard_decisions.py`.
- Add `tests/test_issue_20_decision_telemetry.py`.

## Implementation

- `StrategyStandard.explain_decision(board, snake_id) -> (move, record)`:
  - includes chosen move, candidate gate records, per-candidate score details, opponent priors, scenario count, expected/worst aggregates, and latency;
  - `decide` stores the record as `last_decision_record`.
- Telemetry:
  - default enabled;
  - disabled by `STANDARD_DECISION_TELEMETRY=0|false|no|off`;
  - path from `STANDARD_DECISION_LOG`, default `logs/standard-decisions.jsonl`;
  - writes asynchronously through a single-worker executor.
- `/move`:
  - appends one `decision` record per turn with game id, turn, snake id, chosen move, variant, fallback flag, and strategy record when available.
- `/end`:
  - appends one `game_end` record with best-effort death cause (`won`, `alive`, `wall`, `starvation`, `body`, `self`, `head_to_head`, `unknown`) using final state plus the last move record for the game.
- Analysis tool:
  - `python3 tools/analyze_standard_decisions.py <log.jsonl>` prints per-game summary metrics;
  - `--game-id ... --turn ...` prints the candidate table for a turn.

## Tests

- StrategyStandard record serializes and contains candidate score breakdowns.
- `/move` writes a JSONL decision record to a configured temp path.
- telemetry disable env prevents writes.
- `/end` writes death-cause game_end records.
- analysis tool summarizes a sample log and prints a turn table.

## Verification

- `python3 -m pytest tests/test_issue_20_decision_telemetry.py`
- `python3 -m pytest tests/test_issue_19_standard_strategy.py tests/test_issue_20_decision_telemetry.py tests/test_dev_snake_server.py`
- CLI smoke against a temporary JSONL log.
