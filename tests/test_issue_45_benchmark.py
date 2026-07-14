from __future__ import annotations

from benchmarks.bench_issue_45_server_concurrency import summarize


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
