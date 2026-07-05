from __future__ import annotations

import argparse
import json
import os
import socket
import statistics
import subprocess
import sys
import time
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[1]
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from benchmarks.battlesnake_payloads import payload_by_name


def rss_kb(pid: int) -> int:
    with open(f"/proc/{pid}/status", "r", encoding="utf-8") as fh:
        for line in fh:
            if line.startswith("VmRSS:"):
                return int(line.split()[1])
    return 0


def wait_for_port(port: int) -> None:
    deadline = time.time() + 10.0
    while time.time() < deadline:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                return
        except OSError:
            time.sleep(0.05)
    raise RuntimeError(f"server did not listen on {port}")


def request_once(port: int, method: str, path: str, body: str = "") -> float:
    body_bytes = body.encode("utf-8")
    request = (
        f"{method} {path} HTTP/1.1\r\n"
        f"Host: 127.0.0.1\r\n"
        f"Connection: close\r\n"
        f"Content-Length: {len(body_bytes)}\r\n"
        f"\r\n"
    ).encode("utf-8") + body_bytes
    started = time.perf_counter()
    with socket.create_connection(("127.0.0.1", port), timeout=5.0) as sock:
        sock.sendall(request)
        chunks: list[bytes] = []
        while True:
            chunk = sock.recv(4096)
            if not chunk:
                break
            chunks.append(chunk)
    elapsed_ms = (time.perf_counter() - started) * 1000.0
    response = b"".join(chunks)
    if b"HTTP/1.1 200 OK" not in response and b"HTTP/1.0 200 OK" not in response:
        raise RuntimeError(response[:200].decode("utf-8", errors="replace"))
    return elapsed_ms


def summarize(values: list[float]) -> dict[str, float]:
    ordered = sorted(values)
    return {
        "min_ms": ordered[0],
        "p50_ms": statistics.median(ordered),
        "p95_ms": ordered[int(round((len(ordered) - 1) * 0.95))],
        "max_ms": ordered[-1],
    }


def run_case(
    server: str,
    command: list[str],
    env: dict[str, str],
    port: int,
    path: str,
    method: str,
    body: str,
    runs: int,
    warmup: int,
) -> dict[str, object]:
    proc = subprocess.Popen(
        command,
        env={**os.environ, **env},
        stdout=subprocess.DEVNULL,
        stderr=subprocess.STDOUT,
    )
    try:
        wait_for_port(port)
        for _ in range(warmup):
            request_once(port, method, path, body)
        values = [request_once(port, method, path, body) for _ in range(runs)]
        return {"server": server, "path": path, "runs": runs, "rss_kb": rss_kb(proc.pid), **summarize(values)}
    finally:
        proc.terminate()
        proc.wait(timeout=10)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--runs", type=int, default=100)
    parser.add_argument("--warmup", type=int, default=10)
    parser.add_argument("--out", type=Path, default=Path("benchmarks/results/http-runtime-baseline.jsonl"))
    args = parser.parse_args()
    if args.runs < 1:
        parser.error("--runs must be at least 1")
    if args.warmup < 0:
        parser.error("--warmup must be non-negative")

    payload = payload_by_name("duel_open_7x7")
    args.out.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(["bash", "tools/build_native_server.sh"], check=True)

    rows = [
        run_case(
            "native",
            ["build/battlesnake-server"],
            {"BATTLESNAKE_PORT": "8092", "BATTLESNAKE_SEARCH_BUDGET_MS": "25"},
            8092,
            "/",
            "GET",
            "",
            args.runs,
            args.warmup,
        ),
        run_case(
            "native",
            ["build/battlesnake-server"],
            {"BATTLESNAKE_PORT": "8092", "BATTLESNAKE_SEARCH_BUDGET_MS": "25"},
            8092,
            "/move",
            "POST",
            payload,
            args.runs,
            args.warmup,
        ),
        run_case(
            "fastapi",
            [sys.executable, "-m", "uvicorn", "battlesnake.main:app", "--host", "127.0.0.1", "--port", "8093"],
            {},
            8093,
            "/",
            "GET",
            "",
            args.runs,
            args.warmup,
        ),
        run_case(
            "fastapi",
            [sys.executable, "-m", "uvicorn", "battlesnake.main:app", "--host", "127.0.0.1", "--port", "8093"],
            {},
            8093,
            "/move",
            "POST",
            payload,
            args.runs,
            args.warmup,
        ),
    ]

    with args.out.open("w", encoding="utf-8") as fh:
        for row in rows:
            line = json.dumps(row, sort_keys=True)
            print(line)
            fh.write(line + "\n")

    native_move = next(row for row in rows if row["server"] == "native" and row["path"] == "/move")
    fastapi_move = next(row for row in rows if row["server"] == "fastapi" and row["path"] == "/move")
    if native_move["p95_ms"] >= fastapi_move["p95_ms"]:
        raise SystemExit("native /move p95 must be lower than fastapi /move p95")
    if native_move["rss_kb"] >= fastapi_move["rss_kb"]:
        raise SystemExit("native RSS must be lower than fastapi RSS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
