# Battlesnake Deploy Runbook

Two snakes are served from `ya.sergeiscv.ru`:

| Snake | Public URL | Runtime | Purpose |
|---|---|---|---|
| Production | `https://ya.sergeiscv.ru/snake/` | Native C server | Ladder play |
| Dev | `https://ya.sergeiscv.ru/test-snake/` | Python FastAPI | Standard FFA strategy experiments |

## Current Deployment

- Server: `ya.sergeiscv.ru`
- Public URL: `https://ya.sergeiscv.ru/snake/`
- Server checkout: `~/deploy/playing-battlesnake`
- Container: `playing-battlesnake`
- Local service port: `8121`
- Container port: `8000`
- Runtime process: `/app/battlesnake-server`
- Current image pattern: `playing-battlesnake:native-<timestamp-or-commit>`

Port `8120` is already used by the MTG backend. Keep Battlesnake on `8121`
unless that service is intentionally moved.

## Native Runtime

The deployed container runs `/app/battlesnake-server`.

Required environment:

```text
BATTLESNAKE_PORT=8000
BATTLESNAKE_SEARCH_BUDGET_MS=400
BATTLESNAKE_WORKERS=2
BATTLESNAKE_QUEUE_CAPACITY=8
```

Native HTTP concurrency is bounded. `BATTLESNAKE_WORKERS` configures `1..64`
move workers (default `2`) and `BATTLESNAKE_QUEUE_CAPACITY` configures the
bounded move FIFO (default `8`). The rejection pool has
`min(move workers + 1, 64)` workers and a rejection FIFO with the same capacity
as the move FIFO. If both user-space FIFOs are full, the listener pauses
`accept()` until its capacity pipe wakes it. Completed handshakes wait in the
kernel listen backlog, which is explicitly `128` in the server; it is another
bounded layer, not an unlimited queue. A connection accepted after the move
FIFO fills receives an explicit `503 Service Unavailable` through the rejection
pool instead of being silently reset.

The listener records a monotonic timestamp immediately after `accept()`. Queue
delay and JSON parsing time are deducted from the request's game timeout before
search. If too little search window remains, the request takes the cheap legal
fallback path rather than starting work that cannot finish in time.

Move request state is not shared between workers: the request and response
buffers, JSON parser, board, arena, search timer, search statistics,
transposition table, and search workspace are constructed for the request. The
space-time scratch declared `_Thread_local` is private to each worker thread.
The configured strategy values are read-only while workers are running.

Each recognized `/move` request emits one atomic JSON telemetry line on stderr:

```json
{"event":"move_request","status":200,"queue_ms":0.008,"handler_ms":348.408,"total_ms":348.486,"timeout_ms":500,"fallback":false}
```

`queue_ms` starts at the accept timestamp, `handler_ms` covers request handling,
and `total_ms` spans accept through response completion. `fallback` is true when
the cheap legal fallback was selected. An accepted overload emits
`{"event":"server_overload","status":503}`. Alert on timeouts/5xx, increasing
fallback rate, or total p99 approaching the game deadline.

### Concurrent capacity gate

Run the production-path gate before deployment or any capacity change:

```bash
python3 -m benchmarks.bench_issue_45_server_concurrency \
  --workers 2 \
  --queue-capacity 8 \
  --concurrency 2 \
  --batches 20 \
  --deadline-ms 500 \
  --search-budget-ms 400 \
  --output /tmp/issue45-server-concurrency.json
```

The gate releases each request pair together, applies a 500 ms external socket
deadline, and fails on a timeout/error/503 or when external/server-total p99
reaches the deadline. Its `server_lifecycle` object records unexpected exit,
final return code, and forced-kill status; any unhealthy lifecycle fails the
gate. The 2026-07-15 local production-build result was 40/40
legal 200 responses, zero timeout/error/503, zero fallback, external p99
`349.444 ms`, server queue p99 `0.169 ms`, handler p99 `348.998 ms`, and total
p99 `349.100 ms`. The measured external safety margin was `150.556 ms`.

Do not increase the worker or queue settings automatically from one result.
Raise capacity only after repeated representative load runs remain below the
deadline and process/thread memory has been measured at the proposed settings.

Routing behavior:

- standard games with exactly two snakes use duel minimax;
- standard games with three or more snakes currently use the safe fallback
  while the experimental native Standard FFA scorer remains parity-gated;
- timeout/error fallback remains first-safe.

Container-local health check:

```bash
curl -sS http://127.0.0.1:8000/
```

Expected response:

```json
{"apiversion":"1","author":"codex","color":"#2563eb","head":"default","tail":"default","version":"0.1.0-native"}
```

## Status Checks

SSH to the server:

```bash
ssh ya.sergeiscv.ru
```

Check the container:

```bash
docker ps --filter name=playing-battlesnake
docker logs --tail=100 playing-battlesnake
```

Check the public endpoint:

```bash
curl -fsS https://ya.sergeiscv.ru/snake/
```

Expected response:

```json
{"apiversion":"1","author":"codex","color":"#2563eb","head":"default","tail":"default","version":"0.1.0-native"}
```

## Rebuild Native Image

Build on `ya.sergeiscv.ru`.

```bash
cd ~/deploy/playing-battlesnake

TAG=playing-battlesnake:native-$(date +%Y%m%d-%H%M%S)

docker build -f battlesnake/Dockerfile -t "$TAG" .
```

## Restart Container

Use the image tag produced by the build step.

```bash
docker rm -f playing-battlesnake

docker run -d \
  --name playing-battlesnake \
  --restart unless-stopped \
  -e BATTLESNAKE_PORT=8000 \
  -e BATTLESNAKE_SEARCH_BUDGET_MS=400 \
  -e BATTLESNAKE_WORKERS=2 \
  -e BATTLESNAKE_QUEUE_CAPACITY=8 \
  -p 0.0.0.0:8121:8000 \
  "$TAG"
```

## Verify Native Process

```bash
pid=$(docker inspect -f '{{.State.Pid}}' playing-battlesnake)
ps -o pid,comm,args -p "$pid"
```

Expected: command includes `/app/battlesnake-server`.

## API Smoke Test

```bash
curl -fsS -H "Content-Type: application/json" \
  -d '{"game":{"id":"smoke","ruleset":{"name":"standard","settings":{}}},"turn":1,"board":{"height":7,"width":7,"food":[{"x":3,"y":3}],"hazards":[],"snakes":[{"id":"me","name":"me","health":100,"body":[{"x":1,"y":1},{"x":1,"y":0}]}]},"you":{"id":"me","name":"me","health":100,"body":[{"x":1,"y":1},{"x":1,"y":0}]}}' \
  https://ya.sergeiscv.ru/snake/move
```

Expected response is a legal move:

```json
{"move":"up"}
```

The exact move can change as strategy logic changes. The important check is a
successful JSON response with a `move` field containing `up`, `down`, `left`,
or `right`.

Standard multi-snake smoke:

```bash
curl -fsS -H "Content-Type: application/json" \
  -d '{"game":{"id":"standard-ffa-smoke","ruleset":{"name":"standard","settings":{}}},"turn":1,"board":{"height":7,"width":7,"food":[{"x":3,"y":3}],"hazards":[],"snakes":[{"id":"me","name":"me","health":100,"body":[{"x":2,"y":2},{"x":2,"y":1},{"x":2,"y":0}]},{"id":"north","name":"north","health":100,"body":[{"x":6,"y":6},{"x":6,"y":5},{"x":6,"y":4}]},{"id":"east","name":"east","health":100,"body":[{"x":6,"y":0},{"x":5,"y":0},{"x":4,"y":0}]}]},"you":{"id":"me","name":"me","health":100,"body":[{"x":2,"y":2},{"x":2,"y":1},{"x":2,"y":0}]}}' \
  https://ya.sergeiscv.ru/snake/move
```

Expected response is any legal move. This specifically exercises the production
standard 3+ snake route and verifies it remains a 200 response while native
Standard FFA parity work is still gated.

## Ladder Observation

After deploying a native image, watch at least the first ladder observation
window before considering the rollout healthy:

- no `/move` timeout or error spikes in container logs;
- no unexpected first-safe fallback surge;
- standard 3+ snake games return legal fallback moves under the configured
  Battlesnake timeout;
- death-cause mix remains consistent with the Python dev-snake evidence from
  `docs/standard-ffa-depth-search-ab.md` and
  `docs/standard-ffa-native-port-report.md`.

## Nginx Route

Config file:

```bash
/etc/nginx/sites-enabled/ya.sergeiscv.ru
```

Route:

```nginx
location = /snake { return 301 /snake/; }
location /snake/ {
    proxy_pass http://127.0.0.1:8121/;
    proxy_http_version 1.1;
    proxy_set_header Host $host;
    proxy_set_header X-Real-IP $remote_addr;
    proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
    proxy_set_header X-Forwarded-Proto $scheme;
    proxy_set_header X-Forwarded-Prefix /snake;
    proxy_read_timeout 120s;
    proxy_connect_timeout 5s;
}
```

After editing nginx:

```bash
sudo nginx -t
sudo systemctl reload nginx
```

## Dev Snake (Python)

The dev snake is the FastAPI app `battlesnake.main:app`, registered on
play.battlesnake.com as a separate snake pointing to
`https://ya.sergeiscv.ru/test-snake/`. It exists to iterate on Standard FFA
strategies in Python before porting them to the native C server (see
`docs/standard-ffa-strategy-spec.md`, Delivery Model).

Environment:

```text
STRATEGY_VARIANT=first-safe     # standard FFA strategy variant
GIT_REVISION=<short sha>        # reported in /  version; auto-detected from git if unset
SNAKE_COLOR=#f59e0b             # dev identity; production snake is #2563eb
MOVE_SAFETY_MARGIN_MS=150       # decide deadline = game timeout - margin
```

Run on the server (pick a free local port, e.g. `8122`; `8120` is the MTG
backend, `8121` is the production snake):

```bash
cd ~/deploy/playing-battlesnake
git pull

python3 -m venv .venv-dev
.venv-dev/bin/pip install -r requirements-dev.txt
.venv-dev/bin/pip install -e .

STRATEGY_VARIANT=first-safe \
GIT_REVISION=$(git rev-parse --short HEAD) \
.venv-dev/bin/uvicorn battlesnake.main:app --host 127.0.0.1 --port 8122
```

The nginx route for `/test-snake/` mirrors the `/snake/` route with
`proxy_pass http://127.0.0.1:8122/;`.

Smoke test:

```bash
curl -fsS https://ya.sergeiscv.ru/test-snake/
```

Expected: `"version":"0.1.0-dev+<variant>.<git rev>"` and the dev color. The
`/move` smoke test payload from the production section works unchanged against
`https://ya.sergeiscv.ru/test-snake/move`.

Behavior notes:

- `/move` runs the strategy under a hard internal deadline
  (`game timeout - MOVE_SAFETY_MARGIN_MS`); on timeout or strategy failure it
  answers with the first safe move instead of erroring, so a bad variant loses
  quality, not games-by-timeout.
- Strategy variants are registered in `STANDARD_VARIANTS` in
  `battlesnake/main.py`; unknown `STRATEGY_VARIANT` values fall back to
  `first-safe` with a warning in logs.

## Rollback

List previous local images:

```bash
docker images 'playing-battlesnake'
```

Restart with a known-good tag:

```bash
docker rm -f playing-battlesnake

docker run -d \
  --name playing-battlesnake \
  --restart unless-stopped \
  -e BATTLESNAKE_PORT=8000 \
  -e BATTLESNAKE_SEARCH_BUDGET_MS=400 \
  -e BATTLESNAKE_WORKERS=2 \
  -e BATTLESNAKE_QUEUE_CAPACITY=8 \
  -p 0.0.0.0:8121:8000 \
  playing-battlesnake:OLD_TAG
```

Then rerun the status checks and smoke test.
