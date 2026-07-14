from __future__ import annotations

import json
import os
import signal
import socket
import subprocess
import tempfile
import threading
import time
from contextlib import closing

import pytest

from benchmarks.battlesnake_payloads import move_payload
from benchmarks.scenarios import SCENARIOS


def _free_port() -> int:
    with closing(socket.socket(socket.AF_INET, socket.SOCK_STREAM)) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def _wait_for_port(port: int) -> None:
    deadline = time.monotonic() + 5.0
    while time.monotonic() < deadline:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.1):
                return
        except OSError:
            time.sleep(0.02)
    raise RuntimeError(f"server did not listen on {port}")


def _start_server(
    port: int,
    server_log: object,
    **environment_overrides: str,
) -> subprocess.Popen[bytes]:
    return subprocess.Popen(
        ["build/battlesnake-server"],
        env={
            **os.environ,
            "BATTLESNAKE_PORT": str(port),
            "BATTLESNAKE_WORKERS": "2",
            "BATTLESNAKE_QUEUE_CAPACITY": "8",
            "BATTLESNAKE_SEARCH_BUDGET_MS": "400",
            **environment_overrides,
        },
        stdout=server_log,
        stderr=subprocess.STDOUT,
    )


def _stop_server(process: subprocess.Popen[bytes]) -> None:
    if process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=2.0)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5.0)


def _blocked_signals(pid: int, tid: int) -> int:
    status = f"/proc/{pid}/task/{tid}/status"
    with open(status, encoding="utf-8") as status_file:
        for line in status_file:
            if line.startswith("SigBlk:"):
                return int(line.split()[1], 16)
    raise AssertionError(f"SigBlk missing from {status}")


def _wait_for_server_socket_count(pid: int, expected: int, *, exact: bool = False) -> None:
    deadline = time.monotonic() + 2.0
    while time.monotonic() < deadline:
        fd_dir = f"/proc/{pid}/fd"
        socket_count = 0
        for entry in os.listdir(fd_dir):
            try:
                target = os.readlink(os.path.join(fd_dir, entry))
            except FileNotFoundError:
                continue
            socket_count += target.startswith("socket:")
        matches_expected = socket_count == expected if exact else socket_count >= expected
        if matches_expected:
            return
        time.sleep(0.005)
    raise AssertionError(f"server did not reach {expected} open sockets")


def _receive_until_close(sock: socket.socket) -> bytes:
    chunks: list[bytes] = []
    while True:
        chunk = sock.recv(4096)
        if not chunk:
            return b"".join(chunks)
        chunks.append(chunk)


def _move_request(body: str) -> bytes:
    body_bytes = body.encode("utf-8")
    return (
        b"POST /move HTTP/1.1\r\n"
        b"Host: 127.0.0.1\r\n"
        b"Content-Type: application/json\r\n"
        + f"Content-Length: {len(body_bytes)}\r\n\r\n".encode()
        + body_bytes
    )


def _simultaneous_post(port: int, request: bytes, barrier: threading.Barrier) -> tuple[int, dict[str, str], float]:
    with socket.create_connection(("127.0.0.1", port), timeout=0.5) as sock:
        sock.settimeout(0.5)
        barrier.wait(timeout=2.0)
        started = time.monotonic()
        sock.sendall(request)
        chunks: list[bytes] = []
        while True:
            chunk = sock.recv(4096)
            if not chunk:
                break
            chunks.append(chunk)
        elapsed_ms = (time.monotonic() - started) * 1000.0

    header, response_body = b"".join(chunks).split(b"\r\n\r\n", 1)
    status = int(header.split(b" ", 2)[1])
    return status, json.loads(response_body), elapsed_ms


def test_simultaneous_move_requests_complete_within_external_deadline() -> None:
    subprocess.run(["bash", "tools/build_native_server.sh"], check=True)
    scenario = next(item for item in SCENARIOS if item.name == "duel_center_pressure_11x11")
    request = _move_request(move_payload(scenario, timeout=500))
    port = _free_port()
    barrier = threading.Barrier(2)

    with tempfile.TemporaryFile(mode="w+") as server_log:
        process = _start_server(port, server_log)
        try:
            _wait_for_port(port)
            results: list[tuple[int, dict[str, str], float] | BaseException | None] = [None, None]

            def run(index: int) -> None:
                try:
                    results[index] = _simultaneous_post(port, request, barrier)
                except BaseException as error:
                    results[index] = error

            threads = [threading.Thread(target=run, args=(index,)) for index in range(2)]
            for thread in threads:
                thread.start()
            for thread in threads:
                thread.join(timeout=2.0)
            assert all(not thread.is_alive() for thread in threads)

            for result in results:
                if isinstance(result, BaseException):
                    raise result
                assert result is not None
                status, body, elapsed_ms = result
                assert status == 200
                assert body["move"] in {"up", "left", "right"}
                assert elapsed_ms < 500.0
        finally:
            _stop_server(process)


def test_worker_threads_block_termination_signals() -> None:
    if not os.path.isdir("/proc/self/task"):
        pytest.skip("Linux /proc task signal masks are required")
    subprocess.run(["bash", "tools/build_native_server.sh"], check=True)
    port = _free_port()
    with tempfile.TemporaryFile(mode="w+") as server_log:
        process = _start_server(port, server_log)
        try:
            _wait_for_port(port)
            task_ids = sorted(int(entry) for entry in os.listdir(f"/proc/{process.pid}/task"))
            assert process.pid in task_ids
            worker_ids = [tid for tid in task_ids if tid != process.pid]
            assert len(worker_ids) == 2

            termination_mask = (1 << (signal.SIGINT - 1)) | (1 << (signal.SIGTERM - 1))
            assert _blocked_signals(process.pid, process.pid) & termination_mask == 0
            for worker_id in worker_ids:
                assert _blocked_signals(process.pid, worker_id) & termination_mask == termination_mask
        finally:
            _stop_server(process)


def test_idle_server_exits_promptly_on_sigterm() -> None:
    subprocess.run(["bash", "tools/build_native_server.sh"], check=True)
    for _ in range(20):
        port = _free_port()
        with tempfile.TemporaryFile(mode="w+") as server_log:
            process = _start_server(port, server_log)
            try:
                _wait_for_port(port)
                started = time.monotonic()
                process.terminate()
                return_code = process.wait(timeout=1.0)
                elapsed_ms = (time.monotonic() - started) * 1000.0
                assert return_code == 0
                assert elapsed_ms < 1000.0
            finally:
                _stop_server(process)


@pytest.mark.parametrize(
    "raw_request",
    [
        (
            b"POST /move HTTP/1.1\r\n"
            b"Host: 127.0.0.1\r\n"
            b"Content-Type: application/json\r\n"
            b"Content-Length: 1\r\n\r\n"
            b"{"
        ),
        (
            b"POST /move HTTP/1.1\r\n"
            b"Host 127.0.0.1\r\n"
            b"Content-Length: 2\r\n\r\n"
            b"{}"
        ),
    ],
    ids=["malformed-json", "malformed-header"],
)
def test_recognized_malformed_move_emits_one_telemetry_line(raw_request: bytes) -> None:
    subprocess.run(["bash", "tools/build_native_server.sh"], check=True)
    port = _free_port()
    with tempfile.TemporaryFile(mode="w+") as server_log:
        process = _start_server(port, server_log)
        try:
            _wait_for_port(port)
            with socket.create_connection(("127.0.0.1", port), timeout=0.5) as sock:
                sock.settimeout(1.0)
                sock.sendall(raw_request)
                response = _receive_until_close(sock)
            assert response.startswith(b"HTTP/1.1 400 Bad Request\r\n")
        finally:
            _stop_server(process)

        server_log.seek(0)
        events = [json.loads(line) for line in server_log if line.startswith("{")]
        move_events = [event for event in events if event.get("event") == "move_request"]
        assert len(move_events) == 1
        assert move_events[0]["status"] == 400
        assert move_events[0]["timeout_ms"] == 0


def test_shutdown_drains_active_and_queued_connections() -> None:
    if not os.path.isdir("/proc/self/task"):
        pytest.skip("Linux /proc process state is required")
    subprocess.run(["bash", "tools/build_native_server.sh"], check=True)
    port = _free_port()
    with tempfile.TemporaryFile(mode="w+") as server_log:
        process = _start_server(
            port,
            server_log,
            BATTLESNAKE_WORKERS="1",
            BATTLESNAKE_QUEUE_CAPACITY="2",
            BATTLESNAKE_IO_TIMEOUT_MS="200",
        )
        active: socket.socket | None = None
        queued: socket.socket | None = None
        try:
            _wait_for_port(port)
            _wait_for_server_socket_count(process.pid, 1, exact=True)
            active = socket.create_connection(("127.0.0.1", port), timeout=0.5)
            queued = socket.create_connection(("127.0.0.1", port), timeout=0.5)
            active.settimeout(2.0)
            queued.settimeout(2.0)
            active.sendall(b"POST /move HTTP/1.1\r\nHost: active\r\n")
            queued.sendall(b"POST /move HTTP/1.1\r\nHost: queued\r\n")
            _wait_for_server_socket_count(process.pid, 3)

            process.terminate()
            active_response = _receive_until_close(active)
            queued_response = _receive_until_close(queued)
            return_code = process.wait(timeout=1.0)

            assert active_response.startswith(b"HTTP/1.1 400 Bad Request\r\n")
            assert queued_response.startswith(b"HTTP/1.1 400 Bad Request\r\n")
            assert return_code == 0
            assert not os.path.exists(f"/proc/{process.pid}")
            with pytest.raises(OSError):
                socket.create_connection(("127.0.0.1", port), timeout=0.1)
        finally:
            if active is not None:
                active.close()
            if queued is not None:
                queued.close()
            _stop_server(process)
