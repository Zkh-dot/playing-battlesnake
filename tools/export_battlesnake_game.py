#!/usr/bin/env python3
"""Export a Battlesnake game replay from a game URL or game id.

The Battlesnake board loads full replay data from:
  https://engine.battlesnake.com/games/<game_id>
  wss://engine.battlesnake.com/games/<game_id>/events

This script intentionally uses only the Python standard library so it can run
in a fresh checkout without installing websocket dependencies.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import socket
import ssl
import struct
import sys
import time
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Any


DEFAULT_ENGINE = "https://engine.battlesnake.com"
DEFAULT_ORIGIN = "https://board.battlesnake.com"
WEBSOCKET_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


class WebSocketError(RuntimeError):
    pass


def parse_game_ref(game_ref: str) -> tuple[str, str | None]:
    """Return (game_id, engine_url_from_query)."""
    game_ref = game_ref.strip()
    parsed = urllib.parse.urlparse(game_ref)
    if not parsed.scheme and "/" not in game_ref and "?" not in game_ref:
        return game_ref, None

    query = urllib.parse.parse_qs(parsed.query)
    engine = query.get("engine", [None])[0]
    game = query.get("game", [None])[0]
    if game:
        return game, engine

    parts = [part for part in parsed.path.split("/") if part]
    if "game" in parts:
        index = parts.index("game")
        if index + 1 < len(parts):
            return parts[index + 1], engine

    raise ValueError(f"could not find a Battlesnake game id in: {game_ref}")


def normalize_engine(engine: str) -> str:
    engine = engine.rstrip("/")
    parsed = urllib.parse.urlparse(engine)
    if not parsed.scheme:
        engine = f"https://{engine}"
    return engine.rstrip("/")


def websocket_url_from_engine(engine: str, game_id: str) -> str:
    parsed = urllib.parse.urlparse(engine)
    if parsed.scheme == "https":
        scheme = "wss"
    elif parsed.scheme == "http":
        scheme = "ws"
    elif parsed.scheme in {"ws", "wss"}:
        scheme = parsed.scheme
    else:
        raise ValueError(f"unsupported engine URL scheme: {parsed.scheme}")

    base_path = parsed.path.rstrip("/")
    path = f"{base_path}/games/{game_id}/events"
    return urllib.parse.urlunparse((scheme, parsed.netloc, path, "", "", ""))


def fetch_json(url: str, timeout: float) -> dict[str, Any]:
    request = urllib.request.Request(url, headers={"User-Agent": "battlesnake-export/1.0"})
    with urllib.request.urlopen(request, timeout=timeout) as response:
        return json.load(response)


class WebSocketClient:
    def __init__(self, url: str, *, origin: str, timeout: float) -> None:
        self.url = url
        self.origin = origin
        self.timeout = timeout
        self.sock: ssl.SSLSocket | socket.socket | None = None
        self.buffer = bytearray()

    def __enter__(self) -> "WebSocketClient":
        self.connect()
        return self

    def __exit__(self, *_exc: object) -> None:
        self.close()

    def connect(self) -> None:
        parsed = urllib.parse.urlparse(self.url)
        if parsed.scheme not in {"ws", "wss"}:
            raise WebSocketError(f"unsupported WebSocket scheme: {parsed.scheme}")
        if not parsed.hostname:
            raise WebSocketError(f"missing WebSocket host: {self.url}")

        port = parsed.port or (443 if parsed.scheme == "wss" else 80)
        raw = socket.create_connection((parsed.hostname, port), timeout=self.timeout)
        raw.settimeout(self.timeout)
        if parsed.scheme == "wss":
            sock: ssl.SSLSocket | socket.socket = ssl.create_default_context().wrap_socket(
                raw,
                server_hostname=parsed.hostname,
            )
        else:
            sock = raw

        key = base64.b64encode(os.urandom(16)).decode("ascii")
        path = urllib.parse.urlunparse(("", "", parsed.path or "/", "", parsed.query, ""))
        host = parsed.netloc
        request = (
            f"GET {path} HTTP/1.1\r\n"
            f"Host: {host}\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {key}\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            f"Origin: {self.origin}\r\n"
            "User-Agent: battlesnake-export/1.0\r\n"
            "\r\n"
        )
        sock.sendall(request.encode("ascii"))
        self.sock = sock
        self._verify_handshake(key)

    def close(self) -> None:
        if self.sock is None:
            return
        try:
            self.sock.close()
        finally:
            self.sock = None

    def _recv(self, size: int) -> bytes:
        if self.sock is None:
            raise WebSocketError("WebSocket is not connected")
        while len(self.buffer) < size:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise WebSocketError("WebSocket closed unexpectedly")
            self.buffer.extend(chunk)
        data = bytes(self.buffer[:size])
        del self.buffer[:size]
        return data

    def _verify_handshake(self, key: str) -> None:
        header = bytearray()
        while b"\r\n\r\n" not in header:
            if self.sock is None:
                raise WebSocketError("WebSocket is not connected")
            chunk = self.sock.recv(4096)
            if not chunk:
                raise WebSocketError("connection closed before WebSocket handshake")
            header.extend(chunk)

        head, pending = bytes(header).split(b"\r\n\r\n", 1)
        self.buffer.extend(pending)
        lines = head.split(b"\r\n")
        status_line = lines[0].decode("latin1", "replace")
        if " 101 " not in status_line:
            body = head.decode("latin1", "replace")
            raise WebSocketError(f"WebSocket handshake failed: {body}")

        headers: dict[str, str] = {}
        for line in lines[1:]:
            if b":" not in line:
                continue
            name, value = line.split(b":", 1)
            headers[name.decode("latin1").lower()] = value.strip().decode("latin1")

        expected = base64.b64encode(
            hashlib.sha1((key + WEBSOCKET_GUID).encode("ascii")).digest()
        ).decode("ascii")
        if headers.get("sec-websocket-accept") != expected:
            raise WebSocketError("WebSocket handshake returned an invalid Sec-WebSocket-Accept header")

    def read_text(self) -> str | None:
        while True:
            opcode, payload = self._read_frame()
            if opcode == 0x8:
                return None
            if opcode == 0x9:
                self._send_control_frame(0xA, payload)
                continue
            if opcode in {0x1, 0x0}:
                return payload.decode("utf-8")

    def _read_frame(self) -> tuple[int, bytes]:
        first = self._recv(2)
        b1, b2 = first
        opcode = b1 & 0x0F
        masked = bool(b2 & 0x80)
        length = b2 & 0x7F
        if length == 126:
            length = struct.unpack("!H", self._recv(2))[0]
        elif length == 127:
            length = struct.unpack("!Q", self._recv(8))[0]

        mask = self._recv(4) if masked else b""
        payload = bytearray(self._recv(length))
        if masked:
            for index in range(length):
                payload[index] ^= mask[index % 4]
        return opcode, bytes(payload)

    def _send_control_frame(self, opcode: int, payload: bytes) -> None:
        if self.sock is None:
            raise WebSocketError("WebSocket is not connected")
        if len(payload) > 125:
            raise WebSocketError("control frame payload is too large")
        mask = os.urandom(4)
        masked = bytearray(payload)
        for index in range(len(masked)):
            masked[index] ^= mask[index % 4]
        self.sock.sendall(bytes([0x80 | opcode, 0x80 | len(masked)]) + mask + masked)


def fetch_events(events_url: str, *, origin: str, timeout: float) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    events: list[dict[str, Any]] = []
    frames: list[dict[str, Any]] = []
    seen_turns: set[int] = set()

    with WebSocketClient(events_url, origin=origin, timeout=timeout) as websocket:
        while True:
            message = websocket.read_text()
            if message is None:
                break

            event = json.loads(message)
            events.append(event)
            if event.get("Type") == "frame":
                frame = event.get("Data")
                if isinstance(frame, dict):
                    turn = frame.get("Turn")
                    if isinstance(turn, int) and turn not in seen_turns:
                        seen_turns.add(turn)
                        frames.append(frame)
            elif event.get("Type") == "game_end":
                break

    frames.sort(key=lambda frame: frame["Turn"])
    return events, frames


def export_game(args: argparse.Namespace) -> dict[str, Any]:
    game_id, engine_from_ref = parse_game_ref(args.game)
    engine = normalize_engine(args.engine or engine_from_ref or DEFAULT_ENGINE)
    game_url = f"{engine}/games/{game_id}"
    events_url = websocket_url_from_engine(engine, game_id)

    metadata = fetch_json(game_url, args.timeout)
    events, frames = fetch_events(events_url, origin=args.origin, timeout=args.timeout)
    if not frames:
        raise RuntimeError(f"no replay frames were returned from {events_url}")

    export = {
        "source": args.game,
        "game_id": game_id,
        "engine_game_url": game_url,
        "engine_events_url": events_url,
        "fetched_at_unix": int(time.time()),
        "game": metadata.get("Game"),
        "last_frame": metadata.get("LastFrame"),
        "frames": frames,
        "events_count": len(events),
    }

    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    json_path = output_dir / f"{game_id}.json"
    ndjson_path = output_dir / f"{game_id}.events.ndjson"
    indent = None if args.compact else 2
    json_path.write_text(json.dumps(export, ensure_ascii=False, indent=indent) + "\n", encoding="utf-8")
    ndjson_path.write_text(
        "".join(json.dumps(event, ensure_ascii=False) + "\n" for event in events),
        encoding="utf-8",
    )

    return {
        "game_id": game_id,
        "json": str(json_path),
        "ndjson": str(ndjson_path),
        "frames": len(frames),
        "events": len(events),
        "first_turn": frames[0]["Turn"],
        "last_turn": frames[-1]["Turn"],
    }


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Export a full Battlesnake game replay.")
    parser.add_argument("game", help="Battlesnake game URL, board URL, or game id")
    parser.add_argument("-o", "--output-dir", default="exports", help="directory for exported files")
    parser.add_argument("--engine", help=f"engine base URL, defaults to {DEFAULT_ENGINE}")
    parser.add_argument("--origin", default=DEFAULT_ORIGIN, help="Origin header for the WebSocket request")
    parser.add_argument("--timeout", type=float, default=30.0, help="socket/request timeout in seconds")
    parser.add_argument("--compact", action="store_true", help="write compact JSON instead of pretty JSON")
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        result = export_game(args)
    except Exception as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
