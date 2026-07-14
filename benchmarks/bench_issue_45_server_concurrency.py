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
DEFAULT_BARRIER_TIMEOUT_SECONDS = 1.0
# Socket work shares one external deadline; this extra interval is only for
# collecting completed futures and cancelling work after that deadline.
DEFAULT_CLEANUP_ALLOWANCE_SECONDS = 0.25


def _percentile(values: list[float], percentile: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    index = max(0, math.ceil(len(ordered) * percentile) - 1)
    return ordered[index]


def summarize(samples: list[dict[str, object]], deadline_ms: int) -> dict[str, object]:
    latencies: list[float] = []
    invalid_latency = False
    for sample in samples:
        value = sample.get("latency_ms")
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            invalid_latency = True
            continue
        latency = float(value)
        if not math.isfinite(latency) or latency < 0:
            invalid_latency = True
            continue
        latencies.append(latency)
    timeout_error_count = sum(
        sample.get("error") is not None or sample.get("status") != 200 for sample in samples
    )
    p99_ms = _percentile(latencies, 0.99)
    failure_reasons: list[str] = []
    if not samples:
        failure_reasons.append("no_samples")
    if invalid_latency:
        failure_reasons.append("invalid_sample_latency")
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


class _BatchTiming:
    def __init__(self, deadline_ms: int) -> None:
        self.deadline_ms = deadline_ms
        self.release_started_at: float | None = None
        self.deadline: float | None = None

    def start(self) -> None:
        self.release_started_at = time.monotonic()
        self.deadline = self.release_started_at + self.deadline_ms / 1000.0


def _error_sample(error: str, started: float | None = None) -> dict[str, object]:
    return {
        "latency_ms": 0.0 if started is None else (time.monotonic() - started) * 1000.0,
        "status": None,
        "error": error,
    }


def _request(
    port: int,
    body: bytes,
    deadline_ms: int,
    barrier: threading.Barrier | None,
    batch_timing: _BatchTiming | None = None,
    barrier_timeout_seconds: float = DEFAULT_BARRIER_TIMEOUT_SECONDS,
) -> dict[str, object]:
    if barrier is not None:
        barrier_started = time.monotonic()
        try:
            barrier.wait(timeout=barrier_timeout_seconds)
        except threading.BrokenBarrierError:
            return _error_sample("barrier_broken", barrier_started)
    started = batch_timing.release_started_at if batch_timing is not None else None
    if started is None:
        started = time.monotonic()
    deadline = batch_timing.deadline if batch_timing is not None else None
    if deadline is None:
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


def run_batch(
    *,
    executor: Any,
    port: int,
    payload: bytes,
    concurrency: int,
    deadline_ms: int,
    barrier_timeout_seconds: float = DEFAULT_BARRIER_TIMEOUT_SECONDS,
    cleanup_allowance_seconds: float = DEFAULT_CLEANUP_ALLOWANCE_SECONDS,
) -> list[dict[str, object]]:
    startup_deadline = time.monotonic() + barrier_timeout_seconds
    batch_timing = _BatchTiming(deadline_ms)
    barrier = threading.Barrier(concurrency + 1, action=batch_timing.start)
    futures: list[concurrent.futures.Future[dict[str, object]]] = []
    samples: list[dict[str, object]] = []
    try:
        for _ in range(concurrency):
            futures.append(
                executor.submit(
                    _request,
                    port,
                    payload,
                    deadline_ms,
                    barrier,
                    batch_timing,
                    barrier_timeout_seconds,
                )
            )
    except Exception as exc:
        barrier.abort()
        samples.extend(
            _error_sample(f"thread_startup_error: {type(exc).__name__}: {exc}")
            for _ in range(concurrency - len(futures))
        )

    if len(futures) == concurrency:
        try:
            barrier.wait(timeout=barrier_timeout_seconds)
        except threading.BrokenBarrierError:
            barrier.abort()

    collection_deadline = (
        batch_timing.deadline + cleanup_allowance_seconds
        if batch_timing.deadline is not None
        else startup_deadline + cleanup_allowance_seconds
    )
    remaining = max(0.0, collection_deadline - time.monotonic())
    _, pending = concurrent.futures.wait(futures, timeout=remaining)
    for future in futures:
        if future in pending:
            future.cancel()
            samples.append(_error_sample("batch_collection_deadline"))
            continue
        try:
            samples.append(future.result())
        except Exception as exc:
            samples.append(_error_sample(f"worker_error: {type(exc).__name__}: {exc}"))
    return samples


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


def wait_for_move_events(
    log_file: Any,
    process: Any,
    *,
    expected: int,
    timeout_seconds: float,
) -> list[dict[str, object]]:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        move_events = [
            event for event in _server_events(log_file) if event.get("event") == "move_request"
        ]
        if len(move_events) >= expected:
            return move_events
        if process.poll() is not None:
            raise RuntimeError(
                f"server exited before warmup move telemetry with status {process.returncode}"
            )
        time.sleep(0.005)
    raise RuntimeError("warmup move telemetry did not appear before bounded deadline")


def _metric_summary(events: list[dict[str, object]], key: str) -> dict[str, float | None]:
    values = [float(event[key]) for event in events if key in event]
    return {
        "p50_ms": _percentile(values, 0.50),
        "p95_ms": _percentile(values, 0.95),
        "p99_ms": _percentile(values, 0.99),
        "max_ms": max(values) if values else None,
    }


def _valid_move_event(event: dict[str, object]) -> bool:
    for key in ("queue_ms", "handler_ms", "total_ms"):
        value = event.get(key)
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            return False
        if not math.isfinite(float(value)) or value < 0:
            return False
    return isinstance(event.get("fallback"), bool)


def assemble_result(
    *,
    configuration: dict[str, int],
    samples: list[dict[str, object]],
    move_events: list[dict[str, object]],
    overload_events: list[dict[str, object]],
    lifecycle: dict[str, object],
) -> dict[str, object]:
    deadline_ms = configuration["deadline_ms"]
    external = summarize(samples, deadline_ms)
    valid_move_events = [event for event in move_events if _valid_move_event(event)]
    server_total_p99 = _percentile(
        [float(event["total_ms"]) for event in valid_move_events], 0.99
    )
    failure_reasons = list(external["failure_reasons"])
    status_503_count = sum(sample.get("status") == 503 for sample in samples)
    if status_503_count:
        failure_reasons.append("http_503")
    if len(move_events) != len(samples):
        failure_reasons.append("missing_move_telemetry")
    if len(valid_move_events) != len(move_events):
        failure_reasons.append("malformed_move_telemetry")
    if server_total_p99 is None or server_total_p99 >= deadline_ms:
        failure_reasons.append("server_total_p99_at_or_above_deadline")
    if lifecycle["unexpected_exit"]:
        failure_reasons.append("unexpected_server_exit")
    if lifecycle["return_code"] != 0:
        failure_reasons.append("server_nonzero_exit")
    if lifecycle["forced_kill"]:
        failure_reasons.append("server_forced_kill")

    return {
        "configuration": configuration,
        "external": external,
        "server": {
            "request_count": len(move_events),
            "queue": _metric_summary(valid_move_events, "queue_ms"),
            "handler": _metric_summary(valid_move_events, "handler_ms"),
            "total": _metric_summary(valid_move_events, "total_ms"),
            "fallback_count": sum(event["fallback"] is True for event in valid_move_events),
            "overload_event_count": len(overload_events),
        },
        "status_503_count": status_503_count,
        "timeout_count": sum(sample.get("error") == "timeout" for sample in samples),
        "error_count": sum(sample.get("error") not in {None, "timeout"} for sample in samples),
        "server_lifecycle": lifecycle,
        "passed": not failure_reasons,
        "failure_reasons": failure_reasons,
    }


def shutdown_server(process: Any, timeout_seconds: float = 5.0) -> dict[str, object]:
    unexpected_exit = process.poll() is not None
    forced_kill = False
    if not unexpected_exit:
        try:
            process.terminate()
        except OSError:
            unexpected_exit = True
        try:
            process.wait(timeout=timeout_seconds)
        except subprocess.TimeoutExpired:
            forced_kill = True
            process.kill()
            process.wait(timeout=timeout_seconds)
    return {
        "unexpected_exit": unexpected_exit,
        "return_code": process.returncode,
        "forced_kill": forced_kill,
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
        warmup_move_event_count = 0
        samples: list[dict[str, object]] = []
        try:
            _wait_for_ready(process, server_log)
            move_events_before_warmup = len(
                [
                    event
                    for event in _server_events(server_log)
                    if event.get("event") == "move_request"
                ]
            )
            warmup = _request(port, payload, deadline_ms, None)
            if warmup["error"] is not None or warmup["status"] != 200:
                raise RuntimeError(f"warmup request failed: {warmup}")
            wait_for_move_events(
                server_log,
                process,
                expected=move_events_before_warmup + 1,
                timeout_seconds=deadline_ms / 1000.0 + DEFAULT_CLEANUP_ALLOWANCE_SECONDS,
            )
            warmup_move_event_count = move_events_before_warmup + 1

            with concurrent.futures.ThreadPoolExecutor(max_workers=concurrency) as executor:
                for _ in range(batches):
                    samples.extend(
                        run_batch(
                            executor=executor,
                            port=port,
                            payload=payload,
                            concurrency=concurrency,
                            deadline_ms=deadline_ms,
                        )
                    )
        finally:
            lifecycle = shutdown_server(process)

        events = _server_events(server_log)

    all_move_events = [event for event in events if event.get("event") == "move_request"]
    move_events = all_move_events[warmup_move_event_count:]
    overload_events = [event for event in events if event.get("event") == "server_overload"]
    return assemble_result(
        configuration={
            "workers": workers,
            "queue_capacity": queue_capacity,
            "concurrency": concurrency,
            "batches": batches,
            "deadline_ms": deadline_ms,
            "search_budget_ms": search_budget_ms,
        },
        samples=samples,
        move_events=move_events,
        overload_events=overload_events,
        lifecycle=lifecycle,
    )


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
    if args.search_budget_ms > 65535:
        parser.error("--search-budget-ms must be at most 65535")

    result = run_benchmark(
        workers=args.workers,
        queue_capacity=args.queue_capacity,
        concurrency=args.concurrency,
        batches=args.batches,
        deadline_ms=args.deadline_ms,
        search_budget_ms=args.search_budget_ms,
    )
    document = json.dumps(result, sort_keys=True, allow_nan=False)
    print(document)
    if args.output is not None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(document + "\n", encoding="utf-8")
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
