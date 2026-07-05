# Battlesnake Deploy Runbook

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
```

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
  -p 0.0.0.0:8121:8000 \
  playing-battlesnake:OLD_TAG
```

Then rerun the status checks and smoke test.
