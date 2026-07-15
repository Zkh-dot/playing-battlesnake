# playing-battlesnake

Native Battlesnake server with a Python development harness.

## Native build

```bash
bash tools/build_native_server.sh
```

## Native run

```bash
BATTLESNAKE_PORT=8000 BATTLESNAKE_SEARCH_BUDGET_MS=400 build/battlesnake-server
```

## Duel evaluation profiles

The reviewable profile sources are
`configs/evaluation_weights/default.json` and
`configs/evaluation_weights/tuned-opponent-pressure.json`. The native registry
is generated deterministically from those complete envelopes at build time;
validate both the sources and generated files with:

```bash
python3 tools/tuning/generate_duel_weight_profiles.py --check
```

Select a compiled profile with
`BATTLESNAKE_DUEL_WEIGHT_SET=<name>@<version>`. When the variable is unset, the
intentional production default is `duel-default@1`. A present empty, malformed,
or unknown selector is startup-fatal before the listener is created and never
falls back to the default. The server does not load arbitrary runtime files.
See `docs/runbooks/battlesnake-deploy.md` for telemetry and deployment checks.

## Tests

```bash
python3 setup.py build_ext --inplace --force
python3 -m unittest discover -v
./tools/run_c_server_tests.sh
python3 -m unittest tests.test_native_server_equivalence -v
```

The native server equivalence test starts a localhost server, so it needs an
environment that allows local socket creation.

## HTTP benchmark

```bash
python3 benchmarks/bench_http_runtime.py --runs 100 --warmup 10 --out benchmarks/results/http-runtime-baseline.jsonl
```

The benchmark compares the native runtime with the development FastAPI app.
Install `requirements-dev.txt` before running FastAPI comparison benchmarks in a
fresh environment.

## Runtime Ownership

Production runtime is the native C executable built by `tools/build_native_server.sh`.
The FastAPI app remains a development comparator for benchmarks and payload behavior checks.
