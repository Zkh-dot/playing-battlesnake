# Battlesnake Deploy Runbook

Current and historical endpoints:

| Endpoint | URL | Current status |
|---|---|---|
| Native production | `http://45.10.166.244:8121/` | Current `playing-battlesnake.service`; do not probe during deploy verification |
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
BATTLESNAKE_BIND_ADDRESS=0.0.0.0
BATTLESNAKE_SEARCH_BUDGET_MS=300
BATTLESNAKE_MOVE_SAFETY_MARGIN_MS=200
BATTLESNAKE_WORKERS=2
BATTLESNAKE_QUEUE_CAPACITY=8
```

`BATTLESNAKE_BIND_ADDRESS` defaults to `0.0.0.0`, preserving the production
listener. It accepts a numeric IPv4 address only and fails startup on an invalid
nonempty value. Candidate verification below uses `127.0.0.1`; setting only a
different port is not isolation because the default listens on every interface.

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

`BATTLESNAKE_IO_TIMEOUT_MS` is an absolute socket-input budget beginning when a
worker starts servicing a connection, not an inactivity timeout reset by every
received byte. Queue wait does not consume that input budget; it remains part
of the separate accept-to-search game deadline above. During shutdown, active
reads and later FIFO-drained reads are interrupted immediately while their
write halves remain available for a bounded graceful response.

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
The runner always binds its temporary server to `127.0.0.1`.

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
MAINPID=$(sudo systemctl show playing-battlesnake.service \
  --property=MainPID --value)
test "$MAINPID" -gt 0
sudo readlink -f "/proc/$MAINPID/exe"
sudo tr '\0' ' ' < "/proc/$MAINPID/cmdline"
sudo ss -H -ltnp 'sport = :8121'
```

These checks inspect unit, process, listener, and journal metadata only. Do not
send health, `/move`, or any other HTTP request to port `8121` during deployment
verification: even a synthetic request consumes a live ladder worker and can
cause a real request to miss its deadline. The separate public route remains
known broken (observed 502 on 2026-07-15); repairing it is outside this native
service verification.

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

## Isolated candidate functional verification

Run functional checks before restarting the ladder service. Select the exact
binary being considered for deployment; for the repository build above that is
the resolved `build/battlesnake-server`. To recheck the currently installed
binary instead, derive it from `/proc/$MAINPID/exe`. Do not guess an install
path.

```bash
CANDIDATE_BINARY=$(readlink -f build/battlesnake-server)
# Installed-binary alternative:
# MAINPID=$(sudo systemctl show playing-battlesnake.service -p MainPID --value)
# CANDIDATE_BINARY=$(sudo readlink -f "/proc/$MAINPID/exe")
export CANDIDATE_BINARY
```

The following procedure binds only loopback port `8129`, applies the exact
production strategy/capacity values, waits for the native readiness record,
checks the kernel listener address, retains logs, and bounds graceful shutdown.
Any failure exits nonzero and the EXIT trap stops the candidate. It never sends
traffic to the ladder listener on port `8121`.

```bash
set -euo pipefail
: "${CANDIDATE_BINARY:?select the exact candidate or installed binary first}"
test -x "$CANDIDATE_BINARY"
CANDIDATE_BINARY=$(readlink -f -- "$CANDIDATE_BINARY")
CANDIDATE_LOG="/tmp/battlesnake-candidate-8129.$$.log"
: >"$CANDIDATE_LOG"
CANDIDATE_PID=""

stop_candidate() {
  if [[ -z "$CANDIDATE_PID" ]]; then
    return 0
  fi
  if kill -0 "$CANDIDATE_PID" 2>/dev/null; then
    kill -TERM "$CANDIDATE_PID"
    stopped=false
    for _ in $(seq 1 50); do
      state=$(ps -o stat= -p "$CANDIDATE_PID" 2>/dev/null || true)
      if [[ -z "$state" || "$state" == *Z* ]]; then
        stopped=true
        break
      fi
      sleep 0.1
    done
    if [[ "$stopped" != true ]]; then
      echo "candidate did not stop within 5 seconds; killing it" >&2
      kill -KILL "$CANDIDATE_PID" 2>/dev/null || true
      wait "$CANDIDATE_PID" 2>/dev/null || true
      CANDIDATE_PID=""
      return 1
    fi
  fi
  result=0
  wait "$CANDIDATE_PID" || result=$?
  CANDIDATE_PID=""
  if [[ "$result" -ne 0 ]]; then
    echo "candidate exited with status $result" >&2
    return 1
  fi
}

cleanup_candidate() {
  original_status=$?
  trap - EXIT
  stop_candidate || original_status=1
  if [[ "$original_status" -ne 0 ]]; then
    tail -n 100 "$CANDIDATE_LOG" >&2 || true
  fi
  echo "candidate log retained at $CANDIDATE_LOG"
  exit "$original_status"
}
trap cleanup_candidate EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

if ss -H -ltn 'sport = :8129' | grep -q .; then
  echo "port 8129 is already occupied" >&2
  exit 1
fi

env \
  BATTLESNAKE_BIND_ADDRESS=127.0.0.1 \
  BATTLESNAKE_PORT=8129 \
  BATTLESNAKE_SEARCH_BUDGET_MS=300 \
  BATTLESNAKE_MOVE_SAFETY_MARGIN_MS=200 \
  BATTLESNAKE_WORKERS=2 \
  BATTLESNAKE_QUEUE_CAPACITY=8 \
  "$CANDIDATE_BINARY" >"$CANDIDATE_LOG" 2>&1 &
CANDIDATE_PID=$!

ready=false
for _ in $(seq 1 100); do
  if grep -Fq \
      'battlesnake native server listening on 127.0.0.1:8129' \
      "$CANDIDATE_LOG"; then
    ready=true
    break
  fi
  if ! kill -0 "$CANDIDATE_PID" 2>/dev/null; then
    echo "candidate exited before readiness" >&2
    exit 1
  fi
  sleep 0.05
done
if [[ "$ready" != true ]]; then
  echo "candidate did not become ready within 5 seconds" >&2
  exit 1
fi

listener=$(ss -H -ltn 'sport = :8129')
if ! grep -Fq '127.0.0.1:8129' <<<"$listener"; then
  echo "candidate did not create the expected loopback listener" >&2
  exit 1
fi
if grep -Fq '0.0.0.0:8129' <<<"$listener"; then
  echo "candidate listener is externally exposed" >&2
  exit 1
fi

curl -fsS --max-time 2 http://127.0.0.1:8129/

response=$(curl -fsS --max-time 2 -H 'Content-Type: application/json' \
  -d '{"game":{"id":"duel-smoke","timeout":500,"ruleset":{"name":"standard","settings":{}}},"turn":1,"board":{"height":7,"width":7,"food":[{"x":3,"y":3}],"hazards":[],"snakes":[{"id":"me","name":"me","health":100,"body":[{"x":1,"y":1},{"x":1,"y":0}]},{"id":"opponent","name":"opponent","health":100,"body":[{"x":5,"y":5},{"x":5,"y":4}]}]},"you":{"id":"me","name":"me","health":100,"body":[{"x":1,"y":1},{"x":1,"y":0}]}}' \
  http://127.0.0.1:8129/move)
printf '%s' "$response" | python3 -c \
  'import json,sys; assert json.load(sys.stdin)["move"] in {"up","down","left","right"}'

response=$(curl -fsS --max-time 2 -H 'Content-Type: application/json' \
  -d '{"game":{"id":"standard-ffa-smoke","timeout":500,"ruleset":{"name":"standard","settings":{}}},"turn":1,"board":{"height":7,"width":7,"food":[{"x":3,"y":3}],"hazards":[],"snakes":[{"id":"me","name":"me","health":100,"body":[{"x":2,"y":2},{"x":2,"y":1},{"x":2,"y":0}]},{"id":"north","name":"north","health":100,"body":[{"x":6,"y":6},{"x":6,"y":5},{"x":6,"y":4}]},{"id":"east","name":"east","health":100,"body":[{"x":6,"y":0},{"x":5,"y":0},{"x":4,"y":0}]}]},"you":{"id":"me","name":"me","health":100,"body":[{"x":2,"y":2},{"x":2,"y":1},{"x":2,"y":0}]}}' \
  http://127.0.0.1:8129/move)
printf '%s' "$response" | python3 -c \
  'import json,sys; assert json.load(sys.stdin)["move"] in {"up","down","left","right"}'

stop_candidate
trap - EXIT INT TERM
echo "candidate verification passed; log retained at $CANDIDATE_LOG"
```

Set the live service values with a systemd drop-in:

```bash
sudo systemctl edit playing-battlesnake.service
```

```ini
[Service]
Environment=BATTLESNAKE_PORT=8121
Environment=BATTLESNAKE_BIND_ADDRESS=0.0.0.0
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
  --property=ExecStart --property=Environment --property=MainPID
sudo ss -H -ltnp 'sport = :8121'
```

Post-restart verification remains metadata-only. Do not send HTTP traffic to
port `8121`; the isolated `8129` procedure is the functional gate for the exact
binary and configuration.

## Ladder Observation

After deploying a native release, watch at least the first ladder observation
window before considering the rollout healthy:

- `playing-battlesnake.service` remains active with the expected main process
  and `8121` listener metadata;
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
```

Do not use the issue #45 deployment verification window to probe this route:
once repaired, it forwards to live port `8121`. Validate public routing under a
separately approved maintenance procedure.

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
```

Run the complete isolated `127.0.0.1:8129` candidate procedure above against
this exact rollback binary before replacing or restarting the live service. If
`ExecStart` names a separately installed copy, then repeat the host's established
install step for that exact path.

If rollback also requires earlier environment values, edit the existing drop-in
with `sudo systemctl edit playing-battlesnake.service` and reload systemd. Then
restart and perform metadata-only verification:

```bash
sudo systemctl daemon-reload
sudo systemctl restart playing-battlesnake.service
sudo systemctl status playing-battlesnake.service
sudo journalctl -u playing-battlesnake.service -n 100 --no-pager
sudo systemctl show playing-battlesnake.service \
  --property=ExecStart --property=Environment --property=MainPID
sudo ss -H -ltnp 'sport = :8121'
```

Do not probe live port `8121` after rollback. Public-route recovery remains a
separate operation and is not required to validate the issue #45 systemd
deployment.
