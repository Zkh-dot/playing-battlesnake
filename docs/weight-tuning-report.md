# Weight Tuning Report

Dataset source:

- `exports/andreammm_games`

Search configuration:

- optimizer: Optuna TPE
- requested trials: 300
- completed trials before plateau stop: 192
- fixed depth: 3
- time budget per minimax call: 5000 ms
- best trial: 73
- best replay score: 0.5841788478073947

Promoted opponent-pressure weights:

```json
{
  "opponent_low_health_food_denial": 0.9747950851938381,
  "opponent_reachable_space": 0.41419471771599625,
  "opponent_safe_moves": 71.36454529204393,
  "territory_delta": 1.8353673505986556
}
```

Compute-node match comparison:

- node: `scv@192.168.1.6`
- matches: 20 generated standard duel boards
- before/default wins: 8
- after/tuned wins: 11
- draws: 1
- before errors: 0
- after errors: 0
- average turns: 107.15

Interpretation:

- The 20-match result is directional, not conclusive. Excluding the draw, tuned weights won 11 of 19 decided games (57.9%), so sampling noise is still high. Use 100+ generated matches or replay-grounded games before treating this as a stable strength estimate.

| match | scenario | after side | winner | turns |
| ---: | --- | ---: | --- | ---: |
| 0 | generated_standard_duel_0 | 0 | after | 104 |
| 1 | generated_standard_duel_1 | 1 | after | 110 |
| 2 | generated_standard_duel_2 | 0 | after | 108 |
| 3 | generated_standard_duel_3 | 1 | after | 120 |
| 4 | generated_standard_duel_4 | 0 | before | 108 |
| 5 | generated_standard_duel_5 | 1 | before | 107 |
| 6 | generated_standard_duel_6 | 0 | before | 104 |
| 7 | generated_standard_duel_7 | 1 | after | 111 |
| 8 | generated_standard_duel_8 | 0 | before | 110 |
| 9 | generated_standard_duel_9 | 1 | after | 104 |
| 10 | generated_standard_duel_10 | 0 | before | 107 |
| 11 | generated_standard_duel_11 | 1 | after | 103 |
| 12 | generated_standard_duel_12 | 0 | before | 106 |
| 13 | generated_standard_duel_13 | 1 | after | 106 |
| 14 | generated_standard_duel_14 | 0 | after | 105 |
| 15 | generated_standard_duel_15 | 1 | draw | 109 |
| 16 | generated_standard_duel_16 | 0 | before | 104 |
| 17 | generated_standard_duel_17 | 1 | before | 109 |
| 18 | generated_standard_duel_18 | 0 | after | 103 |
| 19 | generated_standard_duel_19 | 1 | after | 105 |
