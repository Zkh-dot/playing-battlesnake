from __future__ import annotations

import argparse
import concurrent.futures
import json
import math
import os
import socket
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path
from typing import Any


PROJECT_ROOT = Path(__file__).resolve().parents[1]
SERVER_BINARY = PROJECT_ROOT / "build" / "battlesnake-server"
FIXTURE_PATH = PROJECT_ROOT / "tests" / "fixtures" / "issue_45_timeout_replay_positions.json"
LEGAL_MOVES = {"up", "down", "left", "right"}


def _percentile(values: list[float], percentile: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    index = max(0, math.ceil(len(ordered) * percentile) - 1)
    return ordered[index]


def summarize(samples: list[dict[str, object]], deadline_ms: int) -> dict[str, object]:
    latencies = [float(sample["latency_ms"]) for sample in samples]
    timeout_error_count = sum(
        sample.get("error") is not None or sample.get("status") != 200 for sample in samples
    )
    p99_ms = _percentile(latencies, 0.99)
    failure_reasons: list[str] = []
    if not samples:
        failure_reasons.append("no_samples")
    if timeout_error_count:
        failure_reasons.append("timeout_or_error")
    if p99_ms is not None and p99_ms >= deadline_ms:
        failure_reasons.append("p99_at_or_above_deadline")
    return {
        "request_count": len(samples),
        "timeout_error_count": timeout_error_count,
        "p50_ms": _percentile(latencies, 0.50),
        "p95_ms": _percentile(latencies, 0.95),
        "p99_ms": p99_ms,
        "max_ms": max(latencies) if latencies else None,
        "deadline_ms": deadline_ms,
        "safety_margin_ms": deadline_ms - p99_ms if p99_ms is not None else None,
        "passed": not failure_reasons,
        "failure_reasons": failure_reasons,
    }


def _free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def _wait_for_ready(process: subprocess.Popen[bytes], log_file: Any) -> None:
    deadline = time.monotonic() + 10.0
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"server exited during startup with status {process.returncode}")
        if b"battlesnake native server listening on" in os.pread(log_file.fileno(), 8192, 0):
            return
        time.sleep(0.02)
    raise RuntimeError("server did not report listening readiness")


def _remaining_seconds(deadline: float) -> float:
    remaining = deadline - time.monotonic()
    if remaining <= 0:
        raise TimeoutError("external request deadline expired")
    return remaining


def _request(port: int, body: bytes, deadline_ms: int, barrier: threading.Barrier | None) -> dict[str, object]:
    if barrier is not None:
        barrier.wait()
    started = time.monotonic()
    deadline = started + deadline_ms / 1000.0
    request = (
        b"POST /move HTTP/1.1\r\n"
        b"Host: 127.0.0.1\r\n"
        b"Connection: close\r\n"
        b"Content-Type: application/json\r\n"
        + f"Content-Length: {len(body)}\r\n\r\n".encode("ascii")
        + body
    )
    status: int | None = None
    error: str | None = None
    response = b""
    try:
        with socket.create_connection(
            ("127.0.0.1", port), timeout=_remaining_seconds(deadline)
        ) as sock:
            sock.settimeout(_remaining_seconds(deadline))
            sock.sendall(request)
            chunks: list[bytes] = []
            while True:
                sock.settimeout(_remaining_seconds(deadline))
                chunk = sock.recv(4096)
                if not chunk:
                    break
                chunks.append(chunk)
            response = b"".join(chunks)
        header, response_body = response.split(b"\r\n\r\n", 1)
        status = int(header.split(b" ", 2)[1])
        parsed = json.loads(response_body)
        if status == 200 and parsed.get("move") not in LEGAL_MOVES:
            error = "illegal_response"
    except (TimeoutError, socket.timeout):
        error = "timeout"
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        error = f"{type(exc).__name__}: {exc}"
    return {
        "latency_ms": (time.monotonic() - started) * 1000.0,
        "status": status,
        "error": error,
    }


def _load_payload(deadline_ms: int) -> bytes:
    fixtures = json.loads(FIXTURE_PATH.read_text(encoding="utf-8"))
    payload = fixtures[0]["payload"]
    payload["game"]["timeout"] = deadline_ms
    return json.dumps(payload, separators=(",", ":"), sort_keys=True).encode("utf-8")


def _server_events(log_file: Any) -> list[dict[str, object]]:
    contents = os.pread(log_file.fileno(), 4 * 1024 * 1024, 0).decode("utf-8", errors="replace")
    events: list[dict[str, object]] = []
    for line in contents.splitlines():
        if not line.startswith("{"):
            continue
        try:
            event = json.loads(line)
        except json.JSONDecodeError:
            continue
        if isinstance(event, dict):
            events.append(event)
    return events


def _metric_summary(events: list[dict[str, object]], key: str) -> dict[str, float | None]:
    values = [float(event[key]) for event in events if key in event]
    return {
        "p50_ms": _percentile(values, 0.50),
        "p95_ms": _percentile(values, 0.95),
        "p99_ms": _percentile(values, 0.99),
        "max_ms": max(values) if values else None,
    }


def run_benchmark(
    *, workers: int, queue_capacity: int, concurrency: int, batches: int,
    deadline_ms: int, search_budget_ms: int,
) -> dict[str, object]:
    subprocess.run(
        ["bash", "tools/build_native_server.sh"],
        cwd=PROJECT_ROOT,
        check=True,
        stdout=subprocess.DEVNULL,
    )
    port = _free_port()
    payload = _load_payload(deadline_ms)
    with tempfile.TemporaryFile() as server_log:
        process = subprocess.Popen(
            [str(SERVER_BINARY)],
            cwd=PROJECT_ROOT,
            env={
                **os.environ,
                "BATTLESNAKE_PORT": str(port),
                "BATTLESNAKE_WORKERS": str(workers),
                "BATTLESNAKE_QUEUE_CAPACITY": str(queue_capacity),
                "BATTLESNAKE_SEARCH_BUDGET_MS": str(search_budget_ms),
            },
            stdout=server_log,
            stderr=subprocess.STDOUT,
        )
        try:
            _wait_for_ready(process, server_log)
            warmup = _request(port, payload, deadline_ms, None)
            if warmup["error"] is not None or warmup["status"] != 200:
                raise RuntimeError(f"warmup request failed: {warmup}")

            samples: list[dict[str, object]] = []
            with concurrent.futures.ThreadPoolExecutor(max_workers=concurrency) as executor:
                for _ in range(batches):
                    barrier = threading.Barrier(concurrency + 1)
                    futures = [
                        executor.submit(_request, port, payload, deadline_ms, barrier)
                        for _ in range(concurrency)
                    ]
                    barrier.wait()
                    samples.extend(future.result() for future in futures)
        finally:
            process.terminate()
            try:
                process.wait(timeout=5.0)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5.0)

        events = _server_events(server_log)

    move_events = [event for event in events if event.get("event") == "move_request"][1:]
    overload_events = [event for event in events if event.get("event") == "server_overload"]
    external = summarize(samples, deadline_ms)
    server_total_p99 = _percentile(
        [float(event["total_ms"]) for event in move_events if "total_ms" in event], 0.99
    )
    failure_reasons = list(external["failure_reasons"])
    status_503_count = sum(sample.get("status") == 503 for sample in samples)
    if status_503_count:
        failure_reasons.append("http_503")
    if len(move_events) != len(samples):
        failure_reasons.append("missing_move_telemetry")
    if server_total_p99 is None or server_total_p99 >= deadline_ms:
        failure_reasons.append("server_total_p99_at_or_above_deadline")

    return {
        "configuration": {
            "workers": workers,
            "queue_capacity": queue_capacity,
            "concurrency": concurrency,
            "batches": batches,
            "deadline_ms": deadline_ms,
            "search_budget_ms": search_budget_ms,
        },
        "external": external,
        "server": {
            "request_count": len(move_events),
            "queue": _metric_summary(move_events, "queue_ms"),
            "handler": _metric_summary(move_events, "handler_ms"),
            "total": _metric_summary(move_events, "total_ms"),
            "fallback_count": sum(event.get("fallback") is True for event in move_events),
            "overload_event_count": len(overload_events),
        },
        "status_503_count": status_503_count,
        "timeout_count": sum(sample.get("error") == "timeout" for sample in samples),
        "error_count": sum(sample.get("error") not in {None, "timeout"} for sample in samples),
        "passed": not failure_reasons,
        "failure_reasons": failure_reasons,
    }


def _positive(parser: argparse.ArgumentParser, option: str, value: int) -> int:
    if value < 1:
        parser.error(f"{option} must be at least 1")
    return value


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Gate native /move concurrent capacity")
    parser.add_argument("--workers", type=int, default=2)
    parser.add_argument("--queue-capacity", type=int, default=8)
    parser.add_argument("--concurrency", type=int, default=2)
    parser.add_argument("--batches", type=int, default=20)
    parser.add_argument("--deadline-ms", type=int, default=500)
    parser.add_argument("--search-budget-ms", type=int, default=400)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)
    for option in (
        "workers", "queue_capacity", "concurrency", "batches", "deadline_ms", "search_budget_ms"
    ):
        setattr(args, option, _positive(parser, f"--{option.replace('_', '-')}", getattr(args, option)))
    if args.workers > 64:
        parser.error("--workers must be at most 64")

    result = run_benchmark(
        workers=args.workers,
        queue_capacity=args.queue_capacity,
        concurrency=args.concurrency,
        batches=args.batches,
        deadline_ms=args.deadline_ms,
        search_budget_ms=args.search_budget_ms,
    )
    document = json.dumps(result, sort_keys=True)
    print(document)
    if args.output is not None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(document + "\n", encoding="utf-8")
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
