# Standard FFA Opponent Model Training

## Input

- Archive: `exports/battlesnake_top150_games_gt50.zip`
- Archive SHA-256: `a31b445c152a33cbaf1617791babc744ec0b4511ef17eb8ee8b298cc56c7cd90`
- Git SHA: `b9bd2891387ab47da51cd0c73d17e78aefbd5ab7`
- Git dirty: `True`
- Replays: `3720`
- Observations: `2239329`
- Candidate rows: `8957316`
- Snakes: `14873`

## Split

```json
{
  "test": 1352660,
  "train": 6292684,
  "validation": 1311972
}
```

Split is deterministic by `game_id`; Task 11 leakage check reported `0` leaked games.

## Models Compared

- `move_prior`
- `logistic`
- `gbdt`

## Selected Model

- Best model: `gbdt`
- Move-prior validation top-1 accuracy: `0.2630`
- Validation top-1 accuracy: `0.6382`
- Test top-1 accuracy: `0.6299`
- Test grouped negative log likelihood: `0.7154`

## Acceptance Gates

- Validation lift over move prior: `0.3751`
- Test drop from validation: `0.0083`
- Share of eligible snakes above 0.35 top-1: `0.9949` over `1377` snakes
- Split leakage: `0` leaked games
- Accepted for future runtime-design discussion: `True`

## Compute Run

- Compute node: `scv@192.168.1.6`
- Hostname: `scv-b760mhdvm2`
- Engine: `polars+lightgbm`
- Threads: `24`
- Python: `3.13.11`
- LightGBM: `4.6.0`
- Polars: `1.42.1`

## Limitations

These metrics are held-out-game metrics for the selected leaderboard population. `snake_name` and `snake_id` are retained as metadata for grouped evaluation slices but are not model input features in the selected training run. Do not treat the offline result as runtime value until a separate integration design shows how prediction latency, artifact loading, and native search interaction will work.
