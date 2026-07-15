from __future__ import annotations

import json
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor

import pytest

from benchmarks import bench_issue_45_server_concurrency as benchmark
from benchmarks.bench_issue_45_server_concurrency import (
    assemble_result,
    run_batch,
    shutdown_server,
    summarize,
    wait_for_move_events,
)


def _sample(latency_ms: object, *, status: int = 200, error: str | None = None) -> dict[str, object]:
    return {"latency_ms": latency_ms, "status": status, "error": error}


def _move_event() -> dict[str, object]:
    return {
        "event": "move_request",
        "queue_ms": 1.0,
        "handler_ms": 2.0,
        "total_ms": 3.0,
        "fallback": False,
        "weight_set": "duel-default",
        "weight_version": "1",
        "weight_sha256": "a" * 64,
    }


def _healthy_lifecycle() -> dict[str, object]:
    return {"unexpected_exit": False, "return_code": 0, "forced_kill": False}


def test_summarize_reports_nearest_rank_latency_and_positive_margin() -> None:
    result = summarize([_sample(float(value)) for value in range(1, 101)], deadline_ms=500)

    assert result == {
        "request_count": 100,
        "timeout_error_count": 0,
        "p50_ms": 50.0,
        "p95_ms": 95.0,
        "p99_ms": 99.0,
        "max_ms": 100.0,
        "deadline_ms": 500,
        "safety_margin_ms": 401.0,
        "passed": True,
        "failure_reasons": [],
    }


def test_summarize_fails_empty_sample_set() -> None:
    result = summarize([], deadline_ms=500)

    assert result["request_count"] == 0
    assert result["timeout_error_count"] == 0
    assert result["passed"] is False
    assert result["failure_reasons"] == ["no_samples"]
    assert result["p99_ms"] is None
    assert result["safety_margin_ms"] is None


def test_summarize_fails_any_timeout_or_error() -> None:
    result = summarize(
        [
            _sample(40.0),
            _sample(500.0, error="timeout"),
            _sample(25.0, status=503),
        ],
        deadline_ms=500,
    )

    assert result["request_count"] == 3
    assert result["timeout_error_count"] == 2
    assert result["passed"] is False
    assert "timeout_or_error" in result["failure_reasons"]


def test_summarize_fails_when_p99_reaches_deadline() -> None:
    result = summarize([_sample(500.0)], deadline_ms=500)

    assert result["p99_ms"] == 500.0
    assert result["safety_margin_ms"] == 0.0
    assert result["passed"] is False
    assert result["failure_reasons"] == ["p99_at_or_above_deadline"]


@pytest.mark.parametrize("latency", [-1.0, float("nan"), float("inf"), "bad"])
def test_summarize_fails_nonfinite_negative_or_nonnumeric_latency(latency: object) -> None:
    result = summarize([_sample(latency)], deadline_ms=500)

    assert result["passed"] is False
    assert result["failure_reasons"] == ["invalid_sample_latency"]
    assert result["p99_ms"] is None
    json.dumps(result, allow_nan=False)


class _FailingSecondSubmit:
    def __init__(self, executor: ThreadPoolExecutor) -> None:
        self.executor = executor
        self.submissions = 0

    def submit(self, function: object, *arguments: object) -> object:
        self.submissions += 1
        if self.submissions == 2:
            raise RuntimeError("synthetic thread startup failure")
        return self.executor.submit(function, *arguments)


def test_run_batch_aborts_broken_barrier_without_stranding_executor() -> None:
    started = time.monotonic()
    with ThreadPoolExecutor(max_workers=2) as executor:
        samples = run_batch(
            executor=_FailingSecondSubmit(executor),
            port=1,
            payload=b"{}",
            concurrency=2,
            deadline_ms=50,
            barrier_timeout_seconds=0.05,
            cleanup_allowance_seconds=0.05,
        )

    assert time.monotonic() - started < 0.5
    assert len(samples) == 2
    assert all(sample["error"] is not None for sample in samples)


def test_run_batch_bounds_controller_barrier_timeout_and_future_collection() -> None:
    started = time.monotonic()
    with ThreadPoolExecutor(max_workers=1) as executor:
        samples = run_batch(
            executor=executor,
            port=1,
            payload=b"{}",
            concurrency=2,
            deadline_ms=50,
            barrier_timeout_seconds=0.05,
            cleanup_allowance_seconds=0.05,
        )

    assert time.monotonic() - started < 0.5
    assert len(samples) == 2
    assert [sample["error"] for sample in samples] == ["barrier_broken", "barrier_broken"]


class _DelayedReleasedBarrier:
    @staticmethod
    def wait(timeout: float) -> None:
        del timeout
        time.sleep(0.04)


def test_measured_latency_includes_worker_delay_after_common_release(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    timing = benchmark._BatchTiming(1000)
    timing.start()
    socket_timeouts: list[float] = []

    def fail_connect(_address: object, *, timeout: float) -> None:
        socket_timeouts.append(timeout)
        raise OSError("synthetic connection failure")

    monkeypatch.setattr(benchmark.socket, "create_connection", fail_connect)

    sample = benchmark._request(
        1,
        b"{}",
        1000,
        _DelayedReleasedBarrier(),
        timing,
        0.1,
    )

    assert float(sample["latency_ms"]) >= 35.0
    assert socket_timeouts
    assert 0.0 < socket_timeouts[0] < 0.98


class _RunningProcess:
    returncode = None

    @staticmethod
    def poll() -> None:
        return None


def test_wait_for_move_events_times_out_promptly_when_warmup_telemetry_is_absent() -> None:
    with tempfile.TemporaryFile() as server_log:
        started = time.monotonic()
        with pytest.raises(RuntimeError, match="warmup move telemetry"):
            wait_for_move_events(server_log, _RunningProcess(), expected=1, timeout_seconds=0.02)
    assert time.monotonic() - started < 0.5


def test_wait_for_move_events_returns_the_known_warmup_event() -> None:
    with tempfile.TemporaryFile() as server_log:
        server_log.write(b'{"event":"move_request","total_ms":1}\n')
        server_log.flush()

        events = wait_for_move_events(
            server_log, _RunningProcess(), expected=1, timeout_seconds=0.02
        )

    assert len(events) == 1


class _FakeProcess:
    def __init__(self, *, return_code: int | None, requires_kill: bool = False) -> None:
        self.returncode = return_code
        self.requires_kill = requires_kill
        self.terminate_calls = 0
        self.kill_calls = 0

    def poll(self) -> int | None:
        return self.returncode

    def terminate(self) -> None:
        self.terminate_calls += 1

    def wait(self, timeout: float) -> int:
        if self.requires_kill and self.kill_calls == 0:
            raise benchmark.subprocess.TimeoutExpired("server", timeout)
        if self.returncode is None:
            self.returncode = 0
        return self.returncode

    def kill(self) -> None:
        self.kill_calls += 1
        self.returncode = -9


def test_shutdown_server_reports_unexpected_preexisting_exit() -> None:
    process = _FakeProcess(return_code=7)

    lifecycle = shutdown_server(process)

    assert lifecycle == {"unexpected_exit": True, "return_code": 7, "forced_kill": False}
    assert process.terminate_calls == 0


def test_shutdown_server_reports_forced_kill_and_final_return_code() -> None:
    process = _FakeProcess(return_code=None, requires_kill=True)

    lifecycle = shutdown_server(process, timeout_seconds=0.01)

    assert lifecycle == {"unexpected_exit": False, "return_code": -9, "forced_kill": True}
    assert process.terminate_calls == 1
    assert process.kill_calls == 1


@pytest.mark.parametrize(
    ("lifecycle", "failure_reason"),
    [
        (
            {"unexpected_exit": True, "return_code": 7, "forced_kill": False},
            "unexpected_server_exit",
        ),
        (
            {"unexpected_exit": False, "return_code": 7, "forced_kill": False},
            "server_nonzero_exit",
        ),
        (
            {"unexpected_exit": False, "return_code": -9, "forced_kill": True},
            "server_forced_kill",
        ),
    ],
)
def test_result_gate_exposes_and_fails_bad_server_lifecycle(
    lifecycle: dict[str, object], failure_reason: str
) -> None:
    result = assemble_result(
        configuration={"deadline_ms": 500},
        samples=[_sample(10.0)],
        move_events=[_move_event()],
        overload_events=[],
        lifecycle=lifecycle,
    )

    assert result["server_lifecycle"] == lifecycle
    assert result["passed"] is False
    assert failure_reason in result["failure_reasons"]


def test_run_benchmark_passes_exact_strategy_environment_to_server(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    captured_environment: dict[str, str] = {}

    class _CapturedProcess:
        pass

    def capture_popen(
        _arguments: object,
        *,
        cwd: object,
        env: dict[str, str],
        stdout: object,
        stderr: object,
    ) -> _CapturedProcess:
        del cwd, stdout, stderr
        captured_environment.update(env)
        return _CapturedProcess()

    monkeypatch.setattr(benchmark.subprocess, "run", lambda *_args, **_kwargs: None)
    monkeypatch.setattr(benchmark.subprocess, "Popen", capture_popen)
    monkeypatch.setattr(benchmark, "_free_port", lambda: 8121)
    monkeypatch.setattr(benchmark, "_load_payload", lambda _deadline_ms: b"{}")
    monkeypatch.setattr(benchmark, "_wait_for_ready", lambda *_args: None)
    monkeypatch.setattr(
        benchmark,
        "_request",
        lambda *_args, **_kwargs: {"status": 200, "error": None},
    )
    monkeypatch.setattr(benchmark, "wait_for_move_events", lambda *_args, **_kwargs: [])
    monkeypatch.setattr(benchmark, "_server_events", lambda _log_file: [])
    monkeypatch.setattr(benchmark, "run_batch", lambda **_kwargs: [])
    monkeypatch.setattr(benchmark, "shutdown_server", lambda _process: _healthy_lifecycle())

    result = benchmark.run_benchmark(
        workers=2,
        queue_capacity=8,
        concurrency=1,
        batches=1,
        deadline_ms=500,
        search_budget_ms=301,
        safety_margin_ms=201,
        duel_weight_set="tuned-opponent-pressure@1",
    )

    assert captured_environment == {
        "BATTLESNAKE_ARENA_BYTES": "262144",
        "BATTLESNAKE_BIND_ADDRESS": "127.0.0.1",
        "BATTLESNAKE_IO_TIMEOUT_MS": "2000",
        "BATTLESNAKE_MAX_REQUEST_BYTES": "196608",
        "BATTLESNAKE_MIN_SEARCH_BUDGET_MS": "50",
        "BATTLESNAKE_MOVE_SAFETY_MARGIN_MS": "201",
        "BATTLESNAKE_PORT": "8121",
        "BATTLESNAKE_QUEUE_CAPACITY": "8",
        "BATTLESNAKE_RESPONSE_BYTES": "4096",
        "BATTLESNAKE_SEARCH_BUDGET_MS": "301",
        "BATTLESNAKE_WORKERS": "2",
        "BATTLESNAKE_DUEL_WEIGHT_SET": "tuned-opponent-pressure@1",
    }
    assert result["configuration"]["search_budget_ms"] == 301
    assert result["configuration"]["safety_margin_ms"] == 201
    assert result["configuration"]["duel_weight_set"] == "tuned-opponent-pressure@1"


def test_result_gate_audits_exact_selected_profile_identity() -> None:
    startup = {
        "event": "server_startup",
        "weight_set": "tuned-opponent-pressure",
        "weight_version": "1",
        "weight_status": "candidate",
        "weight_sha256": "6996443271beb18e835355a72ad02e2ee9d47148a33e14370ce096fc766f1c5d",
    }
    move = {
        **_move_event(),
        "weight_set": "tuned-opponent-pressure",
        "weight_sha256": startup["weight_sha256"],
    }

    result = assemble_result(
        configuration={"deadline_ms": 500, "duel_weight_set": "tuned-opponent-pressure@1"},
        samples=[_sample(10.0)],
        move_events=[move],
        overload_events=[],
        lifecycle=_healthy_lifecycle(),
        startup_events=[startup],
        expected_duel_weight_set="tuned-opponent-pressure@1",
        profile_audit_events=[move],
    )

    assert result["passed"] is True
    assert result["duel_weight_profile"] == {
        "name": "tuned-opponent-pressure",
        "version": "1",
        "status": "candidate",
        "sha256": "6996443271beb18e835355a72ad02e2ee9d47148a33e14370ce096fc766f1c5d",
    }


@pytest.mark.parametrize(
    ("field", "wrong_value"),
    [
        ("weight_set", "duel-default"),
        ("weight_version", "2"),
        ("weight_status", "production-default"),
        ("weight_sha256", "b" * 64),
    ],
)
def test_result_gate_fails_any_exact_profile_identity_mismatch(
    field: str, wrong_value: str
) -> None:
    startup = {
        "event": "server_startup",
        "weight_set": "tuned-opponent-pressure",
        "weight_version": "1",
        "weight_status": "candidate",
        "weight_sha256": "6996443271beb18e835355a72ad02e2ee9d47148a33e14370ce096fc766f1c5d",
    }
    startup[field] = wrong_value
    exact_move = {
        **_move_event(),
        "weight_set": "tuned-opponent-pressure",
        "weight_sha256": "6996443271beb18e835355a72ad02e2ee9d47148a33e14370ce096fc766f1c5d",
    }
    result = assemble_result(
        configuration={"deadline_ms": 500, "duel_weight_set": "tuned-opponent-pressure@1"},
        samples=[_sample(10.0)],
        move_events=[exact_move],
        overload_events=[],
        lifecycle=_healthy_lifecycle(),
        startup_events=[startup],
        expected_duel_weight_set="tuned-opponent-pressure@1",
    )

    assert result["passed"] is False
    assert "duel_weight_profile_exact_mismatch" in result["failure_reasons"]


def test_result_gate_audits_warmup_profile_identity_without_counting_its_latency() -> None:
    exact = _move_event()
    wrong_warmup = {**exact, "weight_sha256": "b" * 64}
    result = assemble_result(
        configuration={"deadline_ms": 500, "duel_weight_set": "duel-default@1"},
        samples=[_sample(10.0)],
        move_events=[exact],
        overload_events=[],
        lifecycle=_healthy_lifecycle(),
        startup_events=[{
            "event": "server_startup",
            "weight_set": "duel-default",
            "weight_version": "1",
            "weight_status": "production-default",
            "weight_sha256": "a51a2213f403f1e21ccb4eb928927bfac72a11acd7e1d52de2f88ef2277f9629",
        }],
        expected_duel_weight_set="duel-default@1",
        profile_audit_events=[wrong_warmup, exact],
    )

    assert result["external"]["request_count"] == 1
    assert result["server"]["request_count"] == 1
    assert result["passed"] is False
    assert "duel_weight_profile_exact_mismatch" in result["failure_reasons"]


def test_cli_accepts_server_maximum_search_budget(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    captured: dict[str, int] = {}

    def fake_run_benchmark(**arguments: int) -> dict[str, object]:
        captured.update(arguments)
        return {"passed": True}

    monkeypatch.setattr(benchmark, "run_benchmark", fake_run_benchmark)

    assert benchmark.main(["--search-budget-ms", "65535"]) == 0
    assert captured["search_budget_ms"] == 65535
    assert json.loads(capsys.readouterr().out) == {"passed": True}


def test_cli_defaults_match_production_strategy_configuration(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    captured: dict[str, int] = {}

    def fake_run_benchmark(**arguments: int) -> dict[str, object]:
        captured.update(arguments)
        return {"passed": True}

    monkeypatch.setattr(benchmark, "run_benchmark", fake_run_benchmark)

    assert benchmark.main([]) == 0
    assert captured["search_budget_ms"] == 300
    assert captured["safety_margin_ms"] == 200
    assert captured["duel_weight_set"] is None
    assert json.loads(capsys.readouterr().out) == {"passed": True}


def test_unset_selector_resolves_the_sole_production_default() -> None:
    assert benchmark._expected_duel_weight_profile(None) == {
        "name": "duel-default",
        "version": "1",
        "status": "production-default",
        "sha256": "a51a2213f403f1e21ccb4eb928927bfac72a11acd7e1d52de2f88ef2277f9629",
    }


def test_cli_accepts_explicit_candidate_weight_set(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    captured: dict[str, object] = {}

    def fake_run_benchmark(**arguments: object) -> dict[str, object]:
        captured.update(arguments)
        return {"passed": True}

    monkeypatch.setattr(benchmark, "run_benchmark", fake_run_benchmark)

    assert benchmark.main(["--duel-weight-set", "tuned-opponent-pressure@1"]) == 0
    assert captured["duel_weight_set"] == "tuned-opponent-pressure@1"
    assert json.loads(capsys.readouterr().out) == {"passed": True}


def test_cli_rejects_unknown_profile_before_building_server(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    called = False

    def unexpected_run_benchmark(**_arguments: object) -> dict[str, object]:
        nonlocal called
        called = True
        return {"passed": True}

    monkeypatch.setattr(benchmark, "run_benchmark", unexpected_run_benchmark)

    with pytest.raises(SystemExit) as error:
        benchmark.main(["--duel-weight-set", "unknown@1"])

    assert error.value.code == 2
    assert called is False
    assert "error: unknown duel weight profile selector: unknown@1" in capsys.readouterr().err


def test_direct_benchmark_script_can_load_profile_contract() -> None:
    result = benchmark.subprocess.run(
        [
            benchmark.sys.executable,
            str(benchmark.PROJECT_ROOT / "benchmarks/bench_issue_45_server_concurrency.py"),
            "--help",
        ],
        cwd=benchmark.PROJECT_ROOT,
        text=True,
        capture_output=True,
    )

    assert result.returncode == 0, result.stderr
    assert "--duel-weight-set" in result.stdout


def test_cli_passes_explicit_strategy_configuration(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    captured: dict[str, int] = {}

    def fake_run_benchmark(**arguments: int) -> dict[str, object]:
        captured.update(arguments)
        return {"passed": True}

    monkeypatch.setattr(benchmark, "run_benchmark", fake_run_benchmark)

    assert benchmark.main(
        ["--search-budget-ms", "301", "--safety-margin-ms", "201"]
    ) == 0
    assert captured["search_budget_ms"] == 301
    assert captured["safety_margin_ms"] == 201
    assert json.loads(capsys.readouterr().out) == {"passed": True}


@pytest.mark.parametrize("safety_margin_ms", [0, 65535])
def test_cli_accepts_server_safety_margin_bounds(
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
    safety_margin_ms: int,
) -> None:
    captured: dict[str, int] = {}

    def fake_run_benchmark(**arguments: int) -> dict[str, object]:
        captured.update(arguments)
        return {"passed": True}

    monkeypatch.setattr(benchmark, "run_benchmark", fake_run_benchmark)

    assert benchmark.main(["--safety-margin-ms", str(safety_margin_ms)]) == 0
    assert captured["safety_margin_ms"] == safety_margin_ms
    assert json.loads(capsys.readouterr().out) == {"passed": True}


def test_cli_rejects_safety_margin_above_server_parser_maximum(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    called = False

    def fake_run_benchmark(**_arguments: int) -> dict[str, object]:
        nonlocal called
        called = True
        return {"passed": True}

    monkeypatch.setattr(benchmark, "run_benchmark", fake_run_benchmark)

    with pytest.raises(SystemExit) as error:
        benchmark.main(["--safety-margin-ms", "65536"])
    assert error.value.code == 2
    assert called is False
    assert "--safety-margin-ms must be at most 65535" in capsys.readouterr().err


def test_cli_rejects_search_budget_above_server_parser_maximum(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    called = False

    def fake_run_benchmark(**_arguments: int) -> dict[str, object]:
        nonlocal called
        called = True
        return {"passed": True}

    monkeypatch.setattr(benchmark, "run_benchmark", fake_run_benchmark)

    with pytest.raises(SystemExit) as error:
        benchmark.main(["--search-budget-ms", "65536"])
    assert error.value.code == 2
    assert called is False
    assert "--search-budget-ms must be at most 65535" in capsys.readouterr().err


def test_cli_refuses_nonstandard_nan_json(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(
        benchmark,
        "run_benchmark",
        lambda **_arguments: {"passed": False, "invalid": float("nan")},
    )

    with pytest.raises(ValueError, match="Out of range float values"):
        benchmark.main([])


@pytest.mark.parametrize(
    "event",
    [
        {"event": "move_request", "queue_ms": 1.0, "handler_ms": 2.0, "total_ms": 3.0},
        {
            "event": "move_request",
            "queue_ms": "not-a-number",
            "handler_ms": 2.0,
            "total_ms": 3.0,
            "fallback": False,
        },
        {
            "event": "move_request",
            "queue_ms": 1.0,
            "handler_ms": -2.0,
            "total_ms": 3.0,
            "fallback": False,
        },
        {
            "event": "move_request",
            "queue_ms": 1.0,
            "handler_ms": 2.0,
            "total_ms": float("nan"),
            "fallback": False,
        },
        {
            "event": "move_request",
            "queue_ms": 1.0,
            "handler_ms": 2.0,
            "total_ms": 3.0,
            "fallback": "false",
        },
    ],
)
def test_result_gate_fails_missing_or_malformed_move_telemetry(event: dict[str, object]) -> None:
    result = assemble_result(
        configuration={"deadline_ms": 500},
        samples=[_sample(10.0)],
        move_events=[event],
        overload_events=[],
        lifecycle=_healthy_lifecycle(),
    )

    assert result["passed"] is False
    assert "malformed_move_telemetry" in result["failure_reasons"]


def test_result_gate_fails_when_samples_outnumber_move_events() -> None:
    result = assemble_result(
        configuration={"deadline_ms": 500},
        samples=[_sample(10.0), _sample(11.0)],
        move_events=[
            {
                "event": "move_request",
                "queue_ms": 1.0,
                "handler_ms": 2.0,
                "total_ms": 3.0,
                "fallback": False,
            }
        ],
        overload_events=[],
        lifecycle=_healthy_lifecycle(),
    )

    assert result["passed"] is False
    assert "missing_move_telemetry" in result["failure_reasons"]


@pytest.mark.parametrize(
    ("samples", "move_events", "failure_reason"),
    [
        (
            [_sample(10.0)],
            [{"event": "move_request"}],
            "malformed_move_telemetry",
        ),
        (
            [_sample(10.0), _sample(11.0)],
            [
                {
                    "event": "move_request",
                    "queue_ms": 1.0,
                    "handler_ms": 2.0,
                    "total_ms": 3.0,
                    "fallback": False,
                }
            ],
            "missing_move_telemetry",
        ),
    ],
    ids=["malformed", "missing"],
)
def test_cli_returns_nonzero_for_invalid_move_telemetry(
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
    samples: list[dict[str, object]],
    move_events: list[dict[str, object]],
    failure_reason: str,
) -> None:
    failed_result = assemble_result(
        configuration={"deadline_ms": 500},
        samples=samples,
        move_events=move_events,
        overload_events=[],
        lifecycle=_healthy_lifecycle(),
    )
    monkeypatch.setattr(benchmark, "run_benchmark", lambda **_arguments: failed_result)

    assert benchmark.main([]) == 1
    emitted = json.loads(capsys.readouterr().out)
    assert emitted["passed"] is False
    assert failure_reason in emitted["failure_reasons"]
