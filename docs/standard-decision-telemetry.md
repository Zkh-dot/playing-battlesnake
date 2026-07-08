# Standard Decision Telemetry

The Python dev snake writes Standard FFA decision telemetry as JSONL by default.

Default log path:

```text
logs/standard-decisions.jsonl
```

Environment controls:

```bash
STANDARD_DECISION_LOG=/path/to/standard-decisions.jsonl
STANDARD_DECISION_TELEMETRY=0  # disables telemetry
```

Per-game metric summary:

```bash
python3 tools/analyze_standard_decisions.py logs/standard-decisions.jsonl
```

Fatal-turn candidate table:

```bash
python3 tools/analyze_standard_decisions.py logs/standard-decisions.jsonl --game-id <game-id> --turn <turn>
```

Local overhead measurement on 2026-07-07:

```text
telemetry enqueue enabled count=500 mean_ms=0.0078 max_ms=0.3256
telemetry disabled count=500 mean_ms=0.0005 max_ms=0.0064
```
