from __future__ import annotations

import fcntl
import json
import os
import signal
import socket
import subprocess
import tempfile
import threading
import time
from contextlib import closing
from pathlib import Path

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


def _wait_for_server_ready(process: subprocess.Popen[bytes], server_log: object) -> None:
    deadline = time.monotonic() + 5.0
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"server exited during startup with status {process.returncode}")
        startup_output = os.pread(server_log.fileno(), 4096, 0)
        if b"battlesnake native server listening on" in startup_output:
            return
        time.sleep(0.02)
    raise RuntimeError("server did not report listening readiness")


def _start_server(
    port: int,
    server_log: object,
    stderr_log: object | int | None = None,
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
        stderr=subprocess.STDOUT if stderr_log is None else stderr_log,
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


def _wait_for_server_socket_count(
    pid: int,
    expected: int,
    *,
    exact: bool = False,
    stable_for: float = 0.0,
) -> None:
    deadline = time.monotonic() + 2.0
    stable_since: float | None = None
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
            now = time.monotonic()
            if stable_since is None:
                stable_since = now
            if now - stable_since >= stable_for:
                return
        else:
            stable_since = None
        time.sleep(0.005)
    if stable_for > 0.0:
        raise AssertionError(
            f"server did not maintain {expected} open sockets for {stable_for:.3f}s"
        )
    raise AssertionError(f"server did not reach {expected} open sockets")


def _wait_for_worker_socket_wait(pid: int, *, stable_for: float = 0.05) -> None:
    deadline = time.monotonic() + 2.0
    stable_since: float | None = None
    observed: set[str] = set()
    inaccessible_seen = False
    while time.monotonic() < deadline:
        worker_ids = [entry for entry in os.listdir(f"/proc/{pid}/task") if int(entry) != pid]
        worker_wchans: list[str] = []
        for worker_id in worker_ids:
            try:
                wchan = Path(f"/proc/{pid}/task/{worker_id}/wchan").read_text(
                    encoding="utf-8"
                ).strip()
            except OSError:
                wchan = ""
            worker_wchans.append(wchan)
            if wchan:
                observed.add(wchan)
            if wchan in {"", "0"}:
                inaccessible_seen = True

        socket_waits = [
            wchan
            for wchan in worker_wchans
            if wchan not in {"", "0"} and "futex" not in wchan.lower()
        ]
        socket_wait = len(socket_waits) == 1
        if socket_wait:
            now = time.monotonic()
            if stable_since is None:
                stable_since = now
            if now - stable_since >= stable_for:
                return
        else:
            stable_since = None
        time.sleep(0.005)
    if inaccessible_seen:
        pytest.skip(f"worker wait channels are inaccessible; observed={sorted(observed)!r}")
    raise AssertionError(
        "server worker did not maintain a socket-read wait channel; "
        f"observed={sorted(observed)!r}"
    )


def _receive_until_close(sock: socket.socket) -> bytes:
    chunks: list[bytes] = []
    while True:
        chunk = sock.recv(4096)
        if not chunk:
            return b"".join(chunks)
        chunks.append(chunk)


def _parse_http_response(response: bytes) -> tuple[int, dict[str, str]]:
    header, response_body = response.split(b"\r\n\r\n", 1)
    status = int(header.split(b" ", 2)[1])
    return status, json.loads(response_body)


def _send_request(port: int, request: bytes, *, timeout: float) -> tuple[int, dict[str, str], float]:
    started = time.monotonic()
    with socket.create_connection(("127.0.0.1", port), timeout=timeout) as sock:
        sock.settimeout(timeout)
        sock.sendall(request)
        response = _receive_until_close(sock)
    elapsed_ms = (time.monotonic() - started) * 1000.0
    status, body = _parse_http_response(response)
    return status, body, elapsed_ms


def _server_events(server_log: object) -> list[dict[str, object]]:
    server_log.seek(0)
    return [json.loads(line) for line in server_log if line.startswith("{")]


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

    status, body = _parse_http_response(b"".join(chunks))
    return status, body, elapsed_ms


def _next_head(head: dict[str, int], move: str) -> tuple[int, int]:
    dx, dy = {
        "up": (0, 1),
        "down": (0, -1),
        "left": (-1, 0),
        "right": (1, 0),
    }[move]
    return head["x"] + dx, head["y"] + dy


def test_timeout_replay_positions_return_expected_safe_move_over_production_http() -> None:
    subprocess.run(["bash", "tools/build_native_server.sh"], check=True)
    fixture_path = Path(__file__).parent / "fixtures" / "issue_45_timeout_replay_positions.json"
    positions = json.loads(fixture_path.read_text(encoding="utf-8"))
    port = _free_port()

    with tempfile.TemporaryFile(mode="w+") as server_log:
        process = _start_server(port, server_log)
        try:
            _wait_for_port(port)
            for position in positions:
                game_turn = f"{position['game_id']} turn {position['turn']}"
                payload = position["payload"]
                request = _move_request(json.dumps(payload, separators=(",", ":")))
                status, body, elapsed_ms = _send_request(port, request, timeout=0.75)

                assert status == 200, game_turn
                assert body["move"] == position["expected_move"], game_turn
                assert body["move"] != position["previous_direction"], game_turn
                next_x, next_y = _next_head(payload["you"]["head"], body["move"])
                assert 0 <= next_x < payload["board"]["width"], game_turn
                assert 0 <= next_y < payload["board"]["height"], game_turn
                assert elapsed_ms < payload["game"]["timeout"], game_turn
        finally:
            _stop_server(process)


def test_queued_move_uses_legal_fallback_with_queue_wait_accounted() -> None:
    subprocess.run(["bash", "tools/build_native_server.sh"], check=True)
    scenario = next(item for item in SCENARIOS if item.name == "duel_center_pressure_11x11")
    request = _move_request(move_payload(scenario, timeout=500))
    port = _free_port()
    barrier = threading.Barrier(2)

    with tempfile.TemporaryFile(mode="w+") as server_log:
        process = _start_server(
            port,
            server_log,
            BATTLESNAKE_WORKERS="1",
            BATTLESNAKE_QUEUE_CAPACITY="2",
            BATTLESNAKE_SEARCH_BUDGET_MS="400",
        )
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
                thread.join(timeout=1.5)
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

        move_events = [
            event
            for event in _server_events(server_log)
            if event.get("event") == "move_request" and event.get("status") == 200
        ]
        assert len(move_events) == 2
        material_queue_delay_ms = 50.0
        queued_events = [
            event for event in move_events if event["queue_ms"] >= material_queue_delay_ms
        ]
        assert len(queued_events) == 1
        assert queued_events[0]["fallback"] is True
        unqueued_events = [event for event in move_events if event is not queued_events[0]]
        assert unqueued_events[0]["fallback"] is False


def test_full_connection_queue_rejects_complete_request_promptly_and_stays_healthy() -> None:
    if not os.path.isdir("/proc/self/fd"):
        pytest.skip("Linux /proc socket state is required")
    subprocess.run(["bash", "tools/build_native_server.sh"], check=True)
    port = _free_port()

    with tempfile.TemporaryFile(mode="w+") as server_log:
        process = _start_server(
            port,
            server_log,
            BATTLESNAKE_WORKERS="1",
            BATTLESNAKE_QUEUE_CAPACITY="1",
            # Outlive the 2s stable-state observation and 0.5s response budget.
            BATTLESNAKE_IO_TIMEOUT_MS="5000",
        )
        active: socket.socket | None = None
        queued: socket.socket | None = None
        rejected: socket.socket | None = None
        try:
            _wait_for_server_ready(process, server_log)
            _wait_for_server_socket_count(process.pid, 1, exact=True)

            active = socket.create_connection(("127.0.0.1", port), timeout=0.5)
            active.sendall(b"POST /move HTTP/1.1\r\nHost: active\r\n")
            _wait_for_server_socket_count(process.pid, 2, exact=True)
            _wait_for_worker_socket_wait(process.pid)

            queued = socket.create_connection(("127.0.0.1", port), timeout=0.5)
            queued.sendall(b"POST /move HTTP/1.1\r\nHost: queued\r\n")
            _wait_for_server_socket_count(process.pid, 3, exact=True, stable_for=0.05)

            scenario = next(item for item in SCENARIOS if item.name == "duel_center_pressure_11x11")
            complete_request = _move_request(move_payload(scenario, timeout=500))
            rejected = socket.create_connection(("127.0.0.1", port), timeout=0.5)
            rejected.settimeout(0.5)
            started = time.monotonic()
            rejected.sendall(complete_request)
            first_response = _receive_until_close(rejected)
            first_elapsed_ms = (time.monotonic() - started) * 1000.0
            first_status, first_body = _parse_http_response(first_response)
            assert first_status == 503
            assert first_body == {}
            assert first_elapsed_ms < 500.0

            status, body, elapsed_ms = _send_request(port, complete_request, timeout=0.5)
            assert status == 503
            assert body == {}
            assert elapsed_ms < 500.0

            rejected.close()
            rejected = None
            active.close()
            active = None
            queued.close()
            queued = None

            _wait_for_server_socket_count(process.pid, 1, exact=True)
            health_status, _, _ = _send_request(
                port,
                b"GET / HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n",
                timeout=0.5,
            )
            assert health_status == 200
        finally:
            if active is not None:
                active.close()
            if queued is not None:
                queued.close()
            if rejected is not None:
                rejected.close()
            _stop_server(process)

        overload_events = [
            event for event in _server_events(server_log) if event.get("event") == "server_overload"
        ]
        assert len(overload_events) == 2
        assert all(event["status"] == 503 for event in overload_events)


def test_saturated_move_capacity_still_returns_prompt_explicit_503() -> None:
    if not os.path.isdir("/proc/self/fd"):
        pytest.skip("Linux /proc socket state is required")
    subprocess.run(["bash", "tools/build_native_server.sh"], check=True)
    port = _free_port()

    with tempfile.TemporaryFile(mode="w+") as server_log:
        process = _start_server(
            port,
            server_log,
            BATTLESNAKE_WORKERS="1",
            BATTLESNAKE_QUEUE_CAPACITY="1",
            BATTLESNAKE_IO_TIMEOUT_MS="5000",
        )
        active: socket.socket | None = None
        queued: socket.socket | None = None
        held_rejections: list[socket.socket] = []
        try:
            _wait_for_server_ready(process, server_log)
            _wait_for_server_socket_count(process.pid, 1, exact=True)

            active = socket.create_connection(("127.0.0.1", port), timeout=0.5)
            active.sendall(b"POST /move HTTP/1.1\r\nHost: active\r\n")
            _wait_for_server_socket_count(process.pid, 2, exact=True)
            _wait_for_worker_socket_wait(process.pid)

            queued = socket.create_connection(("127.0.0.1", port), timeout=0.5)
            queued.sendall(b"POST /move HTTP/1.1\r\nHost: queued\r\n")
            _wait_for_server_socket_count(process.pid, 3, exact=True, stable_for=0.05)

            scenario = next(item for item in SCENARIOS if item.name == "duel_center_pressure_11x11")
            complete_request = _move_request(move_payload(scenario, timeout=500))
            time.sleep(0.55)
            _wait_for_server_socket_count(process.pid, 3, exact=True, stable_for=0.05)
            for _ in range(12):
                held = socket.create_connection(("127.0.0.1", port), timeout=0.5)
                held.settimeout(0.5)
                started = time.monotonic()
                held.sendall(complete_request)
                held_status, held_body = _parse_http_response(_receive_until_close(held))
                elapsed_ms = (time.monotonic() - started) * 1000.0
                assert held_status == 503
                assert held_body == {}
                assert elapsed_ms < 300.0
                held_rejections.append(held)

            for held in held_rejections:
                held.close()
            held_rejections.clear()
            active.close()
            active = None
            queued.close()
            queued = None

            _wait_for_server_socket_count(process.pid, 1, exact=True)
            health_status, _, _ = _send_request(
                port,
                b"GET / HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n",
                timeout=0.5,
            )
            assert health_status == 200
        finally:
            if active is not None:
                active.close()
            if queued is not None:
                queued.close()
            for held in held_rejections:
                held.close()
            _stop_server(process)

        overload_events = [
            event for event in _server_events(server_log) if event.get("event") == "server_overload"
        ]
        assert len(overload_events) == 12
        assert all(event["status"] == 503 for event in overload_events)


def test_unread_overload_telemetry_pipe_never_blocks_prompt_503() -> None:
    if not os.path.isdir("/proc/self/fd"):
        pytest.skip("Linux /proc socket state is required")
    subprocess.run(["bash", "tools/build_native_server.sh"], check=True)
    port = _free_port()
    telemetry_read_fd, telemetry_write_fd = os.pipe()
    fcntl.fcntl(telemetry_write_fd, fcntl.F_SETPIPE_SZ, 4096)

    with tempfile.TemporaryFile(mode="w+") as server_log:
        process = _start_server(port, server_log, stderr_log=telemetry_write_fd)
        os.close(telemetry_write_fd)
        telemetry_write_fd = -1
        active: socket.socket | None = None
        queued: socket.socket | None = None
        try:
            _wait_for_server_ready(process, server_log)
            _wait_for_server_socket_count(process.pid, 1, exact=True)

            active = socket.create_connection(("127.0.0.1", port), timeout=0.5)
            active.sendall(b"POST /move HTTP/1.1\r\nHost: active\r\n")
            _wait_for_server_socket_count(process.pid, 2, exact=True)
            _wait_for_worker_socket_wait(process.pid)

            queued = socket.create_connection(("127.0.0.1", port), timeout=0.5)
            queued.sendall(b"POST /move HTTP/1.1\r\nHost: queued\r\n")
            _wait_for_server_socket_count(process.pid, 3, exact=True, stable_for=0.05)

            scenario = next(item for item in SCENARIOS if item.name == "duel_center_pressure_11x11")
            complete_request = _move_request(move_payload(scenario, timeout=500))
            for _ in range(160):
                status, body, elapsed_ms = _send_request(port, complete_request, timeout=0.5)
                assert status == 503
                assert body == {}
                assert elapsed_ms < 300.0

            active.close()
            active = None
            queued.close()
            queued = None
            _wait_for_server_socket_count(process.pid, 1, exact=True)
            health_status, _, _ = _send_request(
                port,
                b"GET / HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n",
                timeout=0.5,
            )
            assert health_status == 200
        finally:
            if active is not None:
                active.close()
            if queued is not None:
                queued.close()
            _stop_server(process)
            if telemetry_write_fd >= 0:
                os.close(telemetry_write_fd)
            os.close(telemetry_read_fd)


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
