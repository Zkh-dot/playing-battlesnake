from __future__ import annotations

import json
import os
import socket
import subprocess
import time
import unittest
from contextlib import closing

from benchmarks.battlesnake_payloads import move_payload, payload_by_name
from benchmarks.scenarios import SCENARIOS


def find_free_port() -> int:
    with closing(socket.socket(socket.AF_INET, socket.SOCK_STREAM)) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def wait_for_port(port: int) -> None:
    deadline = time.time() + 5.0
    while time.time() < deadline:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                return
        except OSError:
            time.sleep(0.05)
    raise RuntimeError(f"server did not listen on {port}")


def post_move(port: int, body: str) -> dict[str, str]:
    body_bytes = body.encode("utf-8")
    request = (
        b"POST /move HTTP/1.1\r\n"
        b"Host: 127.0.0.1\r\n"
        b"Content-Type: application/json\r\n"
        + f"Content-Length: {len(body_bytes)}\r\n\r\n".encode("utf-8")
        + body_bytes
    )
    with socket.create_connection(("127.0.0.1", port), timeout=2.0) as sock:
        sock.sendall(request)
        chunks: list[bytes] = []
        while True:
            chunk = sock.recv(4096)
            if not chunk:
                break
            chunks.append(chunk)

    response = b"".join(chunks)
    header, payload = response.split(b"\r\n\r\n", 1)
    assert b"HTTP/1.1 200 OK" in header
    return json.loads(payload.decode("utf-8"))


class NativeServerEquivalenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        subprocess.run(["bash", "tools/build_native_server.sh"], check=True)

    def test_native_server_returns_valid_moves_for_existing_scenarios(self) -> None:
        port = find_free_port()
        proc = subprocess.Popen(
            ["build/battlesnake-server"],
            env={**os.environ, "BATTLESNAKE_PORT": str(port), "BATTLESNAKE_SEARCH_BUDGET_MS": "25"},
            stdout=subprocess.DEVNULL,
            stderr=subprocess.STDOUT,
        )
        try:
            wait_for_port(port)
            for name in ("duel_open_7x7", "duel_center_pressure_11x11", "standard_four_snakes_dense"):
                with self.subTest(name=name):
                    response = post_move(port, payload_by_name(name))
                    self.assertIn(response["move"], {"up", "down", "left", "right"})
        finally:
            proc.terminate()
            proc.wait(timeout=5)

    def test_native_server_clamps_search_budget_to_request_timeout(self) -> None:
        port = find_free_port()
        proc = subprocess.Popen(
            ["build/battlesnake-server"],
            env={**os.environ, "BATTLESNAKE_PORT": str(port), "BATTLESNAKE_SEARCH_BUDGET_MS": "400"},
            stdout=subprocess.DEVNULL,
            stderr=subprocess.STDOUT,
        )
        try:
            wait_for_port(port)
            scenario = next(item for item in SCENARIOS if item.name == "duel_center_pressure_11x11")
            body = move_payload(scenario, timeout=180)

            started = time.monotonic()
            response = post_move(port, body)
            elapsed_ms = (time.monotonic() - started) * 1000

            self.assertIn(response["move"], {"up", "down", "left", "right"})
            self.assertLess(elapsed_ms, 180)
        finally:
            proc.terminate()
            proc.wait(timeout=5)
