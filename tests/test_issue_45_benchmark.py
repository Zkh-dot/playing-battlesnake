from __future__ import annotations

import json

import pytest

from benchmarks import bench_issue_45_server_concurrency as benchmark
from benchmarks.bench_issue_45_server_concurrency import assemble_result, summarize


def _sample(latency_ms: float, *, status: int = 200, error: str | None = None) -> dict[str, object]:
    return {"latency_ms": latency_ms, "status": status, "error": error}


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
    )

    assert result["passed"] is False
    assert "malformed_move_telemetry" in result["failure_reasons"]


def test_cli_returns_nonzero_for_malformed_move_telemetry(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    failed_result = assemble_result(
        configuration={"deadline_ms": 500},
        samples=[_sample(10.0)],
        move_events=[{"event": "move_request"}],
        overload_events=[],
    )
    monkeypatch.setattr(benchmark, "run_benchmark", lambda **_arguments: failed_result)

    assert benchmark.main([]) == 1
    emitted = json.loads(capsys.readouterr().out)
    assert emitted["passed"] is False
    assert "malformed_move_telemetry" in emitted["failure_reasons"]
