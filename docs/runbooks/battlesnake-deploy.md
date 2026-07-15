# Battlesnake Deploy Runbook

Current and historical endpoints:

| Endpoint | URL | Current status |
|---|---|---|
| Native production | `http://45.10.166.244:8121/` | Current `playing-battlesnake.service`; use for deployment verification |
| Public production route | `https://ya.sergeiscv.ru/snake/` | Stale/broken: observed HTTP 502 on 2026-07-15 |
| Historical dev route | `https://ya.sergeiscv.ru/test-snake/` | Reference only; verify independently before use |

DNS for `ya.sergeiscv.ru` points to a different host from `45.10.166.244`.
The public route is therefore not an alias for the native service host and is
not a success gate for the issue #45 native deployment.

## Current Deployment

- Native service host: `45.10.166.244`
- Direct native endpoint: `http://45.10.166.244:8121/`
- Runtime: systemd unit `playing-battlesnake.service`
- Native service port: `8121`

The checkout and executable paths are properties of the installed unit, not a
repository convention. Discover them with `systemctl show` before updating;
do not assume a home directory or Docker path.

Port `8120` is already used by the MTG backend. Keep Battlesnake on `8121`
unless that service is intentionally moved.

## Native Runtime

The deployed `playing-battlesnake.service` runs the native server named by its
`ExecStart` property.

Required environment:

```text
BATTLESNAKE_PORT=8121
BATTLESNAKE_SEARCH_BUDGET_MS=300
BATTLESNAKE_MOVE_SAFETY_MARGIN_MS=200
BATTLESNAKE_WORKERS=2
BATTLESNAKE_QUEUE_CAPACITY=8
```

Native HTTP concurrency is bounded. `BATTLESNAKE_WORKERS` configures `1..64`
move workers (default `2`) and `BATTLESNAKE_QUEUE_CAPACITY` configures the
bounded move FIFO (default `8`). A connection accepted after that FIFO fills
receives a fixed `503 Service Unavailable` through a nonblocking send followed
by a write half-close and immediate close. Rejection retains no per-request
thread, buffer, queue slot, or file descriptor. If the small nonblocking send
cannot complete because the socket already has an error or no send capacity,
the connection is closed and the overload telemetry event is still emitted.

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

`queue_ms` starts at the accept timestamp. `handler_ms` starts after socket input
finishes and covers parsing, search, and response construction; it excludes
socket reads and writes. `total_ms` spans accept through response write
completion. `fallback` is true when the cheap legal fallback was selected. An
accepted overload emits
`{"event":"server_overload","status":503}`. Alert on timeouts/5xx, increasing
fallback rate, or total p99 approaching the game deadline.

Telemetry is best-effort under sink backpressure. Stderr is configured
nonblocking at startup, and every JSON event is emitted with one bounded raw
write so a full pipe cannot stall request acceptance or workers. A line may be
dropped on `EAGAIN`, a partial write, or another sink error; production logging
must monitor collector health in addition to event-derived alerts. The server
fails startup if stderr cannot be configured nonblocking.

### Concurrent capacity gate

Run the production-path gate before deployment or any capacity change:

```bash
python3 -m benchmarks.bench_issue_45_server_concurrency \
  --workers 2 \
  --queue-capacity 8 \
  --concurrency 2 \
  --batches 20 \
  --deadline-ms 500 \
  --search-budget-ms 300 \
  --safety-margin-ms 200 \
  --output /tmp/issue45-server-concurrency.json
```

The benchmark defaults for search budget and safety margin are the current
production values (`300` and `200` ms); both are written explicitly above so a
copied gate command remains an auditable snapshot of the tested service profile.

The gate releases each request pair together, applies a 500 ms external socket
deadline, and fails on a timeout/error/503 or when external/server-total p99
reaches the deadline. Its `server_lifecycle` object records unexpected exit,
final return code, and forced-kill status; any unhealthy lifecycle fails the
gate. The 2026-07-15 local production-profile result was 40/40
legal 200 responses, zero timeout/error/503, zero fallback, external p99
`299.204 ms`, server queue p99 `0.230 ms`, handler p99 `298.665 ms`, and total
p99 `298.817 ms`. The measured external deadline margin was `200.796 ms`.

Do not increase the worker or queue settings automatically from one result.
Raise capacity only after repeated representative load runs remain below the
deadline and process/thread memory has been measured at the proposed settings.

Routing behavior:

- standard games with exactly two snakes use duel minimax;
- standard games with three or more snakes currently use the safe fallback
  while the experimental native Standard FFA scorer remains parity-gated;
- timeout/error fallback remains first-safe.

Service-local health check on the production host:

```bash
curl -fsS http://127.0.0.1:8121/
```

Expected response:

```json
{"apiversion":"1","author":"codex","color":"#2563eb","head":"default","tail":"default","version":"0.1.0-native"}
```

## Status Checks

SSH to the server:

```bash
ssh <production-user>@45.10.166.244
```

Do not assume a checkout or binary location. Inspect the current unit and its
effective configuration first:

```bash
sudo systemctl cat playing-battlesnake.service
sudo systemctl show playing-battlesnake.service \
  --property=FragmentPath \
  --property=WorkingDirectory \
  --property=ExecStart \
  --property=Environment
sudo systemctl status playing-battlesnake.service
sudo journalctl -u playing-battlesnake.service -n 100 --no-pager
```

Check the native port. This is the required deployment status check:

```bash
curl -fsS http://45.10.166.244:8121/
```

Expected response:

```json
{"apiversion":"1","author":"codex","color":"#2563eb","head":"default","tail":"default","version":"0.1.0-native"}
```

The public route is a separate repair concern. As of 2026-07-15 it resolves to
a different host and returns 502; record its status without treating failure as
a failed native deployment:

```bash
curl -sS -o /dev/null -w '%{http_code}\n' https://ya.sergeiscv.ru/snake/
```

## Update the Native systemd Service

On `45.10.166.244`, derive the deployment path from the unit. Stop if
`WorkingDirectory` is empty or `ExecStart` does not identify the expected
native binary; inspect the unit's existing deployment procedure instead of
inventing a path.

```bash
WORKDIR=$(sudo systemctl show playing-battlesnake.service \
  --property=WorkingDirectory --value)
sudo systemctl show playing-battlesnake.service --property=ExecStart --value
test -n "$WORKDIR"
cd "$WORKDIR"

git status --short
git fetch origin
git checkout main
git pull --ff-only origin main
bash tools/build_native_server.sh
```

Confirm that the resulting binary is the executable shown by `ExecStart`. If
the unit points to a separately installed copy, use that host's established
install step to update that exact path before restarting.

Set the live service values with a systemd drop-in:

```bash
sudo systemctl edit playing-battlesnake.service
```

```ini
[Service]
Environment=BATTLESNAKE_PORT=8121
Environment=BATTLESNAKE_SEARCH_BUDGET_MS=300
Environment=BATTLESNAKE_MOVE_SAFETY_MARGIN_MS=200
Environment=BATTLESNAKE_WORKERS=2
Environment=BATTLESNAKE_QUEUE_CAPACITY=8
```

Then restart and inspect the actual service:

```bash
sudo systemctl daemon-reload
sudo systemctl restart playing-battlesnake.service
sudo systemctl status playing-battlesnake.service
sudo journalctl -u playing-battlesnake.service -n 100 --no-pager
sudo systemctl show playing-battlesnake.service \
  --property=ExecStart --property=Environment
curl -fsS http://127.0.0.1:8121/
```

## Production API Smoke Test (direct native endpoint)

```bash
curl -fsS -H "Content-Type: application/json" \
  -d '{"game":{"id":"smoke","ruleset":{"name":"standard","settings":{}}},"turn":1,"board":{"height":7,"width":7,"food":[{"x":3,"y":3}],"hazards":[],"snakes":[{"id":"me","name":"me","health":100,"body":[{"x":1,"y":1},{"x":1,"y":0}]}]},"you":{"id":"me","name":"me","health":100,"body":[{"x":1,"y":1},{"x":1,"y":0}]}}' \
  http://45.10.166.244:8121/move
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
  http://45.10.166.244:8121/move
```

Expected response is any legal move. This specifically exercises the production
standard 3+ snake route and verifies it remains a 200 response while native
Standard FFA parity work is still gated.

## Ladder Observation

After deploying a native release, watch at least the first ladder observation
window before considering the rollout healthy:

- the direct `http://45.10.166.244:8121/` health and `/move` smoke tests remain
  successful; do not substitute the currently broken public route for this check;
- no `/move` timeout or error spikes in
  `journalctl -u playing-battlesnake.service`;
- no unexpected first-safe fallback surge;
- standard 3+ snake games return legal fallback moves under the configured
  Battlesnake timeout;
- death-cause mix remains consistent with the Python dev-snake evidence from
  `docs/standard-ffa-depth-search-ab.md` and
  `docs/standard-ffa-native-port-report.md`.

## Public-route repair reference

The `/snake/` nginx route is not on the native service host: it belongs to the
separate host serving DNS for `ya.sergeiscv.ru`, and it was returning 502 on
2026-07-15. The following is repair/reference configuration, not a description
of verified current state. On that public-route host, first discover the active
nginx configuration (a historical candidate path is shown below):

```bash
sudo nginx -T
# Historical candidate: /etc/nginx/sites-enabled/ya.sergeiscv.ru
```

The production upstream must target the native service host, not loopback on
the separate public-route host:

```nginx
location = /snake { return 301 /snake/; }
location /snake/ {
    proxy_pass http://45.10.166.244:8121/;
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

After repairing the configuration on the public-route host:

```bash
sudo nginx -t
sudo systemctl reload nginx
curl -fsS https://ya.sergeiscv.ru/snake/
```

## Dev Snake (Python)

Historically, the dev snake used the FastAPI app `battlesnake.main:app` and the
route `https://ya.sergeiscv.ru/test-snake/` for Standard FFA experiments before
porting strategies to the native C server (see
`docs/standard-ffa-strategy-spec.md`, Delivery Model). This section preserves
that development recipe; it does not assert that the route or process is live.

Environment:

```text
STRATEGY_VARIANT=first-safe     # standard FFA strategy variant
GIT_REVISION=<short sha>        # reported in /  version; auto-detected from git if unset
SNAKE_COLOR=#f59e0b             # dev identity; production snake is #2563eb
MOVE_SAFETY_MARGIN_MS=150       # decide deadline = game timeout - margin
```

On a chosen dev host, use its actual checkout path and a free local port. The
placeholder below is intentional; do not infer the native production checkout:

```bash
cd <dev-checkout>
git pull

python3 -m venv .venv-dev
.venv-dev/bin/pip install -r requirements-dev.txt
.venv-dev/bin/pip install -e .

STRATEGY_VARIANT=first-safe \
GIT_REVISION=$(git rev-parse --short HEAD) \
.venv-dev/bin/uvicorn battlesnake.main:app --host 127.0.0.1 --port 8122
```

Any `/test-snake/` proxy host and upstream must be rediscovered before restoring
the historical route; do not assume it shares loopback with this dev process.

After explicitly restoring that separate route, its diagnostic smoke test is:

```bash
curl -fsS https://ya.sergeiscv.ru/test-snake/
```

A healthy restored route reports `"version":"0.1.0-dev+<variant>.<git rev>"`
and the dev color. Until the route has been restored and verified, this command
is diagnostic rather than a production deployment gate.

Behavior notes:

- `/move` runs the strategy under a hard internal deadline
  (`game timeout - MOVE_SAFETY_MARGIN_MS`); on timeout or strategy failure it
  answers with the first safe move instead of erroring, so a bad variant loses
  quality, not games-by-timeout.
- Strategy variants are registered in `STANDARD_VARIANTS` in
  `battlesnake/main.py`; unknown `STRATEGY_VARIANT` values fall back to
  `first-safe` with a warning in logs.

## Rollback

Record a known-good commit before deployment. To roll back code, use the same
`WorkingDirectory` and `ExecStart` discovered from the unit; do not substitute
a guessed checkout or a retired container image.

```bash
WORKDIR=$(sudo systemctl show playing-battlesnake.service \
  --property=WorkingDirectory --value)
test -n "$WORKDIR"
cd "$WORKDIR"
git checkout <known-good-commit>
bash tools/build_native_server.sh

# If ExecStart names a separately installed copy, repeat the host's established
# install step for that exact path before restarting.
sudo systemctl restart playing-battlesnake.service
sudo systemctl status playing-battlesnake.service
sudo journalctl -u playing-battlesnake.service -n 100 --no-pager
```

If the rollback also requires earlier environment values, edit the existing
drop-in with `sudo systemctl edit playing-battlesnake.service`, run
`sudo systemctl daemon-reload`, and restart again. Then rerun the direct native-
port status check and both direct native-port smoke tests. Public-route recovery
remains a separate operation and is not required to validate the issue #45
systemd deployment.
