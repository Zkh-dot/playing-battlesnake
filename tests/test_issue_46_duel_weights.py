from __future__ import annotations

import json
import math
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from contextlib import closing
from pathlib import Path

import pytest

from battlesnake.battlesnake_native import (
    Board,
    Coord,
    Snake,
    duel_weight_profiles,
    evaluate,
    minimax_diagnostics,
)
from tools.tuning.duel_weight_profiles import (
    WEIGHT_KEYS,
    DuelWeightProfile,
    canonical_weights_sha256,
    load_profile,
    validate_profiles,
)
from tools.tuning.render_duel_weight_ab_report import render_report


ROOT = Path(__file__).resolve().parents[1]
CONFIG_DIR = ROOT / "configs" / "evaluation_weights"
PROFILE_PATHS = [CONFIG_DIR / "default.json", CONFIG_DIR / "tuned-opponent-pressure.json"]
REPLAY_FIXTURE = ROOT / "tests" / "fixtures" / "issue_46_duel_weight_replays.json"
ENVELOPE_KEYS = {"schema_version", "name", "version", "status", "weights"}
INTEGRITY_MANIFEST = ROOT / "configs/evaluation_weights/duel_weight_profiles.sha256"


def raw_default() -> dict[str, object]:
    return json.loads(PROFILE_PATHS[0].read_text())


def test_profiles_have_strict_complete_envelopes() -> None:
    profiles = [load_profile(path) for path in PROFILE_PATHS]
    assert [profile.status for profile in profiles] == ["production-default", "candidate"]
    for path, profile in zip(PROFILE_PATHS, profiles, strict=True):
        raw = json.loads(path.read_text())
        assert set(raw) == ENVELOPE_KEYS
        assert raw["schema_version"] == 1
        assert tuple(profile.weights) == WEIGHT_KEYS
        assert all(isinstance(value, (int, float)) and not isinstance(value, bool) for value in profile.weights.values())
        assert all(math.isfinite(value) for value in profile.weights.values())
        assert profile.name and profile.version
        assert canonical_weights_sha256(profile.weights) == profile.sha256


@pytest.mark.parametrize(
    ("mutate", "match"),
    [
        (lambda value: value.update(extra=1), "envelope"),
        (lambda value: value.pop("version"), "envelope"),
        (lambda value: value.update(schema_version=2), "schema_version"),
        (lambda value: value.update(name="bad name"), "name"),
        (lambda value: value.update(status="default"), "status"),
        (lambda value: value["weights"].update(extra=1.0), "weight keys"),
        (lambda value: value["weights"].pop("base"), "weight keys"),
        (lambda value: value["weights"].update(base=True), "number"),
        (lambda value: value["weights"].update(base="500"), "number"),
    ],
)
def test_profile_validation_rejects_invalid_data(tmp_path: Path, mutate, match: str) -> None:
    raw = raw_default()
    mutate(raw)
    path = tmp_path / "invalid.json"
    path.write_text(json.dumps(raw))
    with pytest.raises(ValueError, match=match):
        load_profile(path)


@pytest.mark.parametrize(
    ("duplicate", "match"),
    [
        ('"name": "shadowed",', "duplicate JSON field: name"),
        ('"weights": {"base": 999.0,', "duplicate JSON field: base"),
    ],
)
def test_profile_loader_rejects_duplicate_json_fields(
    tmp_path: Path,
    duplicate: str,
    match: str,
) -> None:
    raw = PROFILE_PATHS[0].read_text()
    if duplicate.startswith('"weights"'):
        raw = raw.replace('"weights": {', duplicate, 1)
    else:
        raw = raw.replace("{", "{" + duplicate, 1)
    path = tmp_path / "duplicate.json"
    path.write_text(raw)

    with pytest.raises(ValueError, match=match):
        load_profile(path)


def test_programmatic_nonfinite_and_registry_invariants_are_rejected() -> None:
    valid = load_profile(PROFILE_PATHS[0])
    invalid = DuelWeightProfile(
        schema_version=1,
        name="not-finite",
        version="1",
        status="candidate",
        weights={**valid.weights, "base": math.inf},
    )
    with pytest.raises(ValueError, match="finite"):
        validate_profiles([valid, invalid])
    with pytest.raises(ValueError, match="duplicate"):
        validate_profiles([valid, valid])
    with pytest.raises(ValueError, match="exactly one production-default"):
        validate_profiles([load_profile(PROFILE_PATHS[1])])


def test_generator_is_current() -> None:
    result = subprocess.run(
        [sys.executable, "tools/tuning/generate_duel_weight_profiles.py", "--check"],
        cwd=ROOT,
        text=True,
        capture_output=True,
    )
    assert result.returncode == 0, result.stderr or result.stdout


def _copy_profile_integrity_tree(tmp_path: Path) -> Path:
    for relative in (
        "configs/evaluation_weights/default.json",
        "configs/evaluation_weights/tuned-opponent-pressure.json",
        "configs/evaluation_weights/duel_weight_profiles.sha256",
        "battlesnake/c-core/core/duel_weight_profiles_generated.c",
        "battlesnake/c-core/core/duel_weight_profiles_generated.h",
        "tools/verify_duel_weight_profiles.sh",
        "tools/build_native_server.sh",
        "tools/tuning/generate_duel_weight_profiles.py",
        "tools/tuning/duel_weight_profiles.py",
        "setup.py",
    ):
        source = ROOT / relative
        destination = tmp_path / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
    return tmp_path


def test_profile_integrity_manifest_and_portable_preflight_are_current() -> None:
    lines = INTEGRITY_MANIFEST.read_text().splitlines()
    assert [line.split("  ", 1)[1] for line in lines] == [
        "configs/evaluation_weights/default.json",
        "configs/evaluation_weights/tuned-opponent-pressure.json",
        "battlesnake/c-core/core/duel_weight_profiles_generated.h",
        "battlesnake/c-core/core/duel_weight_profiles_generated.c",
    ]
    result = subprocess.run(
        ["bash", "tools/verify_duel_weight_profiles.sh"],
        cwd=ROOT,
        text=True,
        capture_output=True,
    )
    assert result.returncode == 0, result.stderr or result.stdout


def test_generator_check_rejects_manifest_drift_in_isolated_tree(tmp_path: Path) -> None:
    tree = _copy_profile_integrity_tree(tmp_path)
    manifest = tree / "configs/evaluation_weights/duel_weight_profiles.sha256"
    manifest.write_text(manifest.read_text() + "# stale\n")

    result = subprocess.run(
        [sys.executable, "tools/tuning/generate_duel_weight_profiles.py", "--check"],
        cwd=tree,
        text=True,
        capture_output=True,
    )

    assert result.returncode != 0
    assert "duel_weight_profiles.sha256" in result.stderr


@pytest.mark.parametrize(
    ("relative", "mutate"),
    [
        (
            "configs/evaluation_weights/default.json",
            lambda path: path.write_text(path.read_text().replace('"schema_version": 1,', '"schema_version": 1,\n  "extra": true,')),
        ),
        (
            "battlesnake/c-core/core/duel_weight_profiles_generated.c",
            lambda path: path.write_text(path.read_text() + "\n/* tampered */\n"),
        ),
    ],
    ids=["malformed-source", "tampered-generated-c"],
)
def test_native_build_preflight_stops_before_compilation_on_integrity_drift(
    tmp_path: Path, relative: str, mutate
) -> None:
    tree = _copy_profile_integrity_tree(tmp_path)
    mutate(tree / relative)
    compiler_marker = tree / "compiler-was-called"
    fake_compiler = tree / "fake-gcc"
    fake_compiler.write_text(f"#!/bin/sh\ntouch '{compiler_marker}'\nexit 99\n")
    fake_compiler.chmod(0o755)

    result = subprocess.run(
        ["bash", "tools/build_native_server.sh"],
        cwd=tree,
        env={**os.environ, "CC": str(fake_compiler)},
        text=True,
        capture_output=True,
    )

    assert result.returncode != 0
    assert "FAILED" in result.stdout + result.stderr
    assert not compiler_marker.exists()


def test_extension_build_runs_strict_generator_check_before_compilation(tmp_path: Path) -> None:
    tree = _copy_profile_integrity_tree(tmp_path)
    source = tree / "configs/evaluation_weights/default.json"
    source.write_text(source.read_text().replace('"schema_version": 1,', '"schema_version": 1,\n  "extra": true,'))

    result = subprocess.run(
        [sys.executable, "setup.py", "build_ext", "--inplace", "--force"],
        cwd=tree,
        text=True,
        capture_output=True,
    )

    assert result.returncode != 0
    assert "envelope keys must be exactly" in result.stdout + result.stderr
    assert "duel_weight_profiles_generated.c" not in result.stderr


def test_native_registry_matches_validated_sources_field_for_field() -> None:
    expected = {(profile.name, profile.version): profile for profile in map(load_profile, PROFILE_PATHS)}
    native = duel_weight_profiles()
    assert len(native) == len(expected)
    for record in native:
        profile = expected[(record["name"], record["version"])]
        assert record["schema_version"] == profile.schema_version
        assert record["status"] == profile.status
        assert record["sha256"] == profile.sha256
        assert tuple(record["weights"]) == WEIGHT_KEYS
        assert record["weights"] == profile.weights
    default = next(record for record in native if record["status"] == "production-default")
    assert all(default["weights"][key] == 0.0 for key in (
        "opponent_reachable_space", "territory_delta", "opponent_safe_moves",
        "opponent_low_health_food_denial",
    ))


def test_generated_profile_has_native_evaluation_parity() -> None:
    board = Board(
        7,
        7,
        [
            Snake("us", "us", 73, [Coord(2, 2), Coord(2, 1), Coord(1, 1)]),
            Snake("them", "them", 41, [Coord(5, 4), Coord(5, 3), Coord(4, 3)]),
        ],
        food=[Coord(3, 2)],
    )
    native_by_id = {(p["name"], p["version"]): p for p in duel_weight_profiles()}
    for path in PROFILE_PATHS:
        profile = load_profile(path)
        native_weights = native_by_id[profile.identifier]["weights"]
        assert evaluate(board, "us", native_weights) == evaluate(board, "us", dict(profile.weights))


def test_replay_fixture_and_report_schema_are_complete() -> None:
    fixtures = json.loads(REPLAY_FIXTURE.read_text())
    assert fixtures["schema_version"] == 1
    assert len(fixtures["positions"]) == 4
    assert {(row["game_id"], row["turn"]) for row in fixtures["positions"]} == {
        ("1197cf21-29c9-43f8-a364-18d9e226fb8c", 197),
        ("8fd97d0d-6f20-436a-833c-062027a12617", 357),
        ("ab3c8a6f-2a94-46f2-a33f-4af6d5b5725a", 326),
        ("be95288a-97e1-401b-a41a-cb79a5afd7b8", 308),
    }
    for row in fixtures["positions"]:
        assert set(row) == {"game_id", "turn", "recorded_move", "snake_id", "board"}
        assert set(row["board"]) == {"width", "height", "ruleset_name", "hazard_damage", "snakes", "food", "hazards"}


def test_replay_report_tool_records_both_profiles_and_repeats(tmp_path: Path) -> None:
    output = tmp_path / "report.json"
    result = subprocess.run(
        [sys.executable, "tools/tuning/report_duel_weight_replays.py", "--fixtures", str(REPLAY_FIXTURE),
         "--budget-ms", "300", "--repeats", "2", "--output", str(output)],
        cwd=ROOT, text=True, capture_output=True,
    )
    assert result.returncode == 0, result.stderr
    payload = json.loads(output.read_text())
    assert payload["settings"] == {"budget_ms": 300, "repeats": 2}
    assert len(payload["records"]) == 16
    assert {row["profile"] for row in payload["records"]} == {"duel-default@1", "tuned-opponent-pressure@1"}
    for row in payload["records"]:
        assert {"game_id", "turn", "repeat", "profile", "move", "depth", "timed_out",
                "elapsed_ms", "selected_structural_proof", "root_comparison",
                "structural_risk", "policy_violation", "error"} <= set(row)


def test_committed_replay_evidence_is_complete_and_error_free() -> None:
    path = ROOT / "docs/evidence/issue-46-duel-weight-replays.json"
    if not path.exists():
        pytest.skip("full frozen evidence is generated after the exact-input tooling commit")
    payload = json.loads(path.read_text())
    assert payload["settings"] == {"budget_ms": 300, "repeats": 5}
    assert len(payload["records"]) == 40
    assert len({(row["game_id"], row["turn"]) for row in payload["records"]}) == 4
    assert {row["profile"] for row in payload["records"]} == {
        "duel-default@1", "tuned-opponent-pressure@1",
    }
    assert all(row["error"] is None for row in payload["records"])


def test_committed_markdown_is_rendered_from_current_evidence_and_registry() -> None:
    if not (ROOT / "docs/evidence/issue-46-duel-weight-ab.json").exists():
        pytest.skip("full frozen evidence is generated after the exact-input tooling commit")
    expected = render_report(
        ROOT / "docs/evidence/issue-46-duel-weight-ab.json",
        ROOT / "docs/evidence/issue-46-duel-weight-replays.json",
    )
    assert (ROOT / "docs/duel-weight-ab-report.md").read_text() == expected
    assert "do not promote" in expected.lower()
    assert "generated production default remains `duel-default@1`" in expected
    evidence = json.loads((ROOT / "docs/evidence/issue-46-duel-weight-ab.json").read_text())
    assert evidence["execution_provenance"] == {
        "preferred_compute_host": "scv@192.168.1.6",
        "attempted_before_local_run": True,
        "ssh_failure": "No route to host",
        "proceeded_locally": True,
        "scenario_count": 100,
        "match_count": 200,
        "sample_reduced": False,
    }
    assert (
        "Preferred compute node `scv@192.168.1.6` was attempted before the local run; "
        "SSH failed with `No route to host`. The experiment therefore proceeded locally "
        "with all 100 scenarios / 200 matches and no sample reduction."
    ) in expected


def test_operator_docs_define_the_duel_profile_runtime_contract() -> None:
    readme = (ROOT / "README.md").read_text()
    runbook = (ROOT / "docs/runbooks/battlesnake-deploy.md").read_text()
    docs = readme + "\n" + runbook

    assert "configs/evaluation_weights/default.json" in docs
    assert "configs/evaluation_weights/tuned-opponent-pressure.json" in docs
    assert "python3 tools/tuning/generate_duel_weight_profiles.py --check" in docs
    assert "python3 tools/tuning/generate_duel_weight_profiles.py\n" in docs
    assert "integrity manifest" in docs.lower()
    assert "native build does not run python" in docs.lower()
    assert "sha256sum" in docs
    assert "both source envelopes" in docs.lower()
    assert "checked-in generated c/h files" in docs.lower()
    assert "extension build and acceptance checks run" in docs.lower()
    assert "ci and acceptance checks run" not in docs.lower()
    assert "build consumes these" not in docs.lower()
    assert "BATTLESNAKE_DUEL_WEIGHT_SET=<name>@<version>" in docs
    assert "duel-default@1" in docs
    assert "unset" in docs.lower()
    assert "empty" in docs.lower()
    assert "malformed" in docs.lower()
    assert "unknown" in docs.lower()
    assert "before the listener" in docs.lower()
    assert "never falls back" in docs.lower()
    assert "server_startup" in docs
    assert "move_request" in docs
    assert "weight_set" in docs
    assert "weight_version" in docs
    assert "weight_sha256" in docs
    assert "weight_status" in docs
    assert "before compilation" in docs.lower()
    assert "arbitrary runtime files" in docs.lower()


def test_tuning_docs_preserve_evidence_and_separate_promotion() -> None:
    report = (ROOT / "docs/weight-tuning-report.md").read_text()

    assert "20-match result is directional, not conclusive" in report
    assert "docs/duel-weight-ab-report.md" in report
    assert "docs/evidence/issue-46-duel-weight-ab.json" in report
    assert "docs/evidence/issue-46-duel-weight-replays.json" in report
    assert "candidate" in report.lower()
    assert "not the production default" in report.lower()
    assert "separate promotion" in report.lower()
    assert "do not promote" in report.lower()
    assert "--seed 46001 --scenario-count 100 --fixed-depth 3" in report
    assert "--time-budget-ms 300 --max-turns 200" in report
    assert "--budget-ms 300 --repeats 5" in report
    assert "200 paired games" in report.lower()
    assert "40 records" in report.lower()
    assert "latency gate" in report.lower()


@pytest.fixture(scope="module")
def native_server() -> Path:
    subprocess.run(["bash", "tools/build_native_server.sh"], cwd=ROOT, check=True)
    return ROOT / "build" / "battlesnake-server"


def _free_port() -> int:
    with closing(socket.socket(socket.AF_INET, socket.SOCK_STREAM)) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def _wait_for_startup(process: subprocess.Popen[bytes], output: object) -> list[str]:
    deadline = time.monotonic() + 5.0
    while time.monotonic() < deadline:
        text = os.pread(output.fileno(), 8192, 0).decode()
        if "battlesnake native server listening on" in text:
            return text.splitlines()
        if process.poll() is not None:
            raise AssertionError(f"server exited during startup: {text}")
        time.sleep(0.02)
    raise AssertionError("server did not report startup")


def _start_server(
    binary: Path,
    output: object,
    selector: str | None,
    *,
    search_budget_ms: int = 1,
) -> tuple[subprocess.Popen[bytes], int, dict[str, object]]:
    port = _free_port()
    env = {
        **os.environ,
        "BATTLESNAKE_BIND_ADDRESS": "127.0.0.1",
        "BATTLESNAKE_PORT": str(port),
        "BATTLESNAKE_SEARCH_BUDGET_MS": str(search_budget_ms),
    }
    if selector is None:
        env.pop("BATTLESNAKE_DUEL_WEIGHT_SET", None)
    else:
        env["BATTLESNAKE_DUEL_WEIGHT_SET"] = selector
    process = subprocess.Popen([binary], env=env, stdout=output, stderr=subprocess.STDOUT)
    lines = _wait_for_startup(process, output)
    startup = next(json.loads(line) for line in lines if line.startswith("{") and "server_startup" in line)
    return process, port, startup


def _stop_server(process: subprocess.Popen[bytes]) -> None:
    if process.poll() is None:
        process.terminate()
        process.wait(timeout=5.0)


def _post(port: int, body: bytes) -> tuple[int, dict[str, object]]:
    request = (
        b"POST /move HTTP/1.1\r\nHost: 127.0.0.1\r\n"
        b"Content-Type: application/json\r\nContent-Length: "
        + str(len(body)).encode()
        + b"\r\n\r\n"
        + body
    )
    with socket.create_connection(("127.0.0.1", port), timeout=2.0) as sock:
        sock.sendall(request)
        response = b""
        while chunk := sock.recv(4096):
            response += chunk
    header, response_body = response.split(b"\r\n\r\n", 1)
    return int(header.split(b" ", 2)[1]), json.loads(response_body)


def _profile_sensitive_fixture() -> tuple[Board, dict[str, object]]:
    me_body = [(3, 3), (4, 3), (4, 4), (5, 4), (5, 3)]
    you_body = [(5, 1), (5, 0), (4, 0), (3, 0)]

    def snake(identifier: str, health: int, body: list[tuple[int, int]]) -> dict[str, object]:
        return {
            "id": identifier,
            "name": identifier,
            "health": health,
            "body": [{"x": x, "y": y} for x, y in body],
        }

    me = snake("me", 74, me_body)
    you = snake("you", 48, you_body)
    board = Board(
        6,
        6,
        [
            Snake("me", "me", 74, [Coord(*point) for point in me_body]),
            Snake("you", "you", 48, [Coord(*point) for point in you_body]),
        ],
        food=[Coord(0, 3), Coord(0, 4)],
    )
    payload = {
        "game": {"id": "profile-wiring", "timeout": 500, "ruleset": {"name": "standard", "settings": {}}},
        "turn": 1,
        "board": {
            "height": 6,
            "width": 6,
            "food": [{"x": 0, "y": 3}, {"x": 0, "y": 4}],
            "hazards": [],
            "snakes": [me, you],
        },
        "you": me,
    }
    return board, payload


@pytest.mark.parametrize(
    ("selector", "expected_name", "expected_status"),
    [(None, "duel-default", "production-default"), ("tuned-opponent-pressure@1", "tuned-opponent-pressure", "candidate")],
)
def test_server_selects_and_reports_immutable_profile(
    native_server: Path,
    selector: str | None,
    expected_name: str,
    expected_status: str,
) -> None:
    with tempfile.TemporaryFile() as output:
        process, _port, startup = _start_server(native_server, output, selector)
        try:
            assert startup["weight_set"] == expected_name
            assert startup["weight_version"] == "1"
            assert startup["weight_status"] == expected_status
            assert re.fullmatch(r"[0-9a-f]{64}", str(startup["weight_sha256"]))
        finally:
            _stop_server(process)


@pytest.mark.parametrize("selector", ["", "missing-at", "@1", "duel-default@", "duel-default@1@extra", "unknown@1"])
def test_invalid_explicit_selector_fails_before_listening(native_server: Path, selector: str) -> None:
    result = subprocess.run(
        [native_server],
        env={**os.environ, "BATTLESNAKE_PORT": str(_free_port()), "BATTLESNAKE_DUEL_WEIGHT_SET": selector},
        text=True,
        capture_output=True,
        timeout=2.0,
    )
    assert result.returncode != 0
    assert "invalid BATTLESNAKE_DUEL_WEIGHT_SET" in result.stderr
    assert "battlesnake native server listening on" not in result.stdout + result.stderr


def test_selected_profile_is_attached_to_all_move_telemetry(native_server: Path) -> None:
    _board, payload = _profile_sensitive_fixture()
    fallback_payload = json.loads(json.dumps(payload))
    fallback_payload["board"]["snakes"].append(
        {
            "id": "third",
            "name": "third",
            "health": 90,
            "body": [{"x": 1, "y": 5}, {"x": 1, "y": 4}],
        }
    )
    for selector in ("duel-default@1", "tuned-opponent-pressure@1"):
        with tempfile.TemporaryFile() as output:
            process, port, startup = _start_server(native_server, output, selector)
            try:
                status, response = _post(port, json.dumps(payload, separators=(",", ":")).encode())
                assert status == 200
                assert response["move"] in {"up", "down", "left", "right"}
                fallback_status, fallback_response = _post(
                    port,
                    json.dumps(fallback_payload, separators=(",", ":")).encode(),
                )
                assert fallback_status == 200
                assert fallback_response["move"] in {"up", "down", "left", "right"}
                malformed_status, _ = _post(port, b"{")
                assert malformed_status == 400
            finally:
                _stop_server(process)
            lines = os.pread(output.fileno(), 16384, 0).decode().splitlines()
            move_events = [json.loads(line) for line in lines if line.startswith("{") and "move_request" in line]
            assert [event["status"] for event in move_events] == [200, 200, 400]
            assert [event["fallback"] for event in move_events] == [False, True, False]
            for event in move_events:
                assert event["weight_set"] == startup["weight_set"]
                assert event["weight_version"] == startup["weight_version"]
                assert event["weight_sha256"] == startup["weight_sha256"]


def test_selected_profile_drives_production_http_move(native_server: Path) -> None:
    board, payload = _profile_sensitive_fixture()
    body = json.dumps(payload, separators=(",", ":")).encode()
    profiles = {
        f"{profile.name}@{profile.version}": profile
        for profile in map(load_profile, PROFILE_PATHS)
    }
    expected_moves = {
        "duel-default@1": "left",
        "tuned-opponent-pressure@1": "down",
    }
    observed_moves: dict[str, set[str]] = {}

    for selector, expected_move in expected_moves.items():
        fixed_depth_moves = {
            minimax_diagnostics(
                board,
                "me",
                time_budget_ms=5000,
                fixed_depth=depth,
                weights=dict(profiles[selector].weights),
            )["move"]
            for depth in range(1, 7)
        }
        assert fixed_depth_moves == {expected_move}

        with tempfile.TemporaryFile() as output:
            process, port, startup = _start_server(
                native_server,
                output,
                selector,
                search_budget_ms=25,
            )
            try:
                observed = [_post(port, body)[1]["move"] for _ in range(10)]
            finally:
                _stop_server(process)
            assert startup["weight_set"] == profiles[selector].name
            observed_moves[selector] = set(observed)
            assert observed_moves[selector] == {expected_move}

    assert len({next(iter(moves)) for moves in observed_moves.values()}) == 2
