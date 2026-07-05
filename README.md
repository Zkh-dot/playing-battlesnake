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
