# Battlesnake Deploy Runbook

## Current Deployment

- Server: `ya.sergeiscv.ru`
- Public URL: `https://ya.sergeiscv.ru/snake/`
- Server checkout: `~/deploy/playing-battlesnake`
- Container: `playing-battlesnake`
- Local service port: `8121`
- Container port: `8000`
- Current image pattern: `playing-battlesnake:icelake-native-<timestamp-or-commit>`
- CPU target: server-local `-march=native -mtune=native`
- Process priority: `nice -n -5`

Port `8120` is already used by the MTG backend. Keep Battlesnake on `8121`
unless that service is intentionally moved.

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
{"apiversion":"1","author":"codex","color":"#2563eb","head":"default","tail":"default","version":"0.1.0"}
```

## Rebuild For Server CPU

Build on `ya.sergeiscv.ru`, not locally. The native C extension should be
compiled on the target CPU.

```bash
cd ~/deploy/playing-battlesnake

TAG=playing-battlesnake:icelake-native-$(date +%Y%m%d-%H%M%S)

docker build -f battlesnake/Dockerfile \
  --build-arg CFLAGS="-O3 -march=native -mtune=native -DNDEBUG" \
  -t "$TAG" .
```

## Restart Container

Use the image tag produced by the build step.

```bash
docker rm -f playing-battlesnake

docker run -d \
  --name playing-battlesnake \
  --restart unless-stopped \
  --cap-add SYS_NICE \
  --cpu-shares 2048 \
  -p 0.0.0.0:8121:8000 \
  "$TAG"
```

`--cap-add SYS_NICE` is required so `nice -n -5` can actually apply inside the
container. `--cpu-shares 2048` gives the container a higher relative CPU share
when the host is under contention.

## Verify Priority

```bash
pid=$(docker inspect -f '{{.State.Pid}}' playing-battlesnake)
ps -o pid,ni,pri,comm,args -p "$pid"
```

Expected: `NI` is `-5` for the `uvicorn` process.

## Verify Native Extension

```bash
docker exec playing-battlesnake python -c \
  "import battlesnake.battlesnake_native as n; print(n.__file__)"
```

Expected: path ends with a compiled `.so`, for example:

```text
/usr/local/lib/python3.11/site-packages/battlesnake/battlesnake_native.cpython-311-x86_64-linux-gnu.so
```

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
successful JSON response with a `move` field.

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
  --cap-add SYS_NICE \
  --cpu-shares 2048 \
  -p 0.0.0.0:8121:8000 \
  playing-battlesnake:OLD_TAG
```

Then rerun the status checks and smoke test.
