from __future__ import annotations

import json
import subprocess
import sys

import pytest

from benchmarks import bench_issue_43_search_budgets as benchmark


def test_json_cli_reports_one_row_per_selected_position_and_budget() -> None:
    completed = subprocess.run(
        [
            sys.executable,
            "-m",
            "benchmarks.bench_issue_43_search_budgets",
            "--repeats",
            "1",
            "--position",
            "0188bbac-t288",
            "--time-budget-ms",
            "1",
            "--node-budget",
            "1",
            "--json",
        ],
        check=False,
        capture_output=True,
        text=True,
    )

    assert completed.returncode == 0, completed.stderr
    assert completed.stderr == ""
    rows = [json.loads(line) for line in completed.stdout.splitlines()]
    assert len(rows) == 2
    assert {(row["budget_kind"], row["budget_value"]) for row in rows} == {
        ("time_ms", 1),
        ("nodes", 1),
    }

    required = {
        "case_id",
        "case",
        "game_id",
        "turn",
        "budget_kind",
        "budget_value",
        "repeat",
        "move",
        "structural_proof",
        "proof_cutoff",
        "completed_depth",
        "max_depth_started",
        "minimax_outcome",
        "minimax_bound",
        "minimax_score",
        "root_comparison_reason",
        "selection_reason",
        "nodes",
        "root_analysis_nodes",
        "elapsed_ms",
    }
    assert all(required <= row.keys() for row in rows)


def test_fixture_defaults_and_position_ids_are_routed_from_committed_data() -> None:
    positions, node_budgets = benchmark.load_fixture()

    assert benchmark.DEFAULT_TIME_BUDGETS == (100, 200, 300)
    assert benchmark.DEFAULT_REPEATS == 3
    assert node_budgets == (16000, 32000, 48000)
    assert [benchmark.case_id(position) for position in positions] == [
        "0188bbac-t288",
        "8fd97d0d-t357",
        "c7add22b-t278",
        "7351410a-t169",
    ]

    assert benchmark.resolve_budgets(None, None, node_budgets) == (
        (100, 200, 300),
        (16000, 32000, 48000),
    )
    assert benchmark.resolve_budgets([25], None, node_budgets) == ((25,), ())
    assert benchmark.resolve_budgets(None, [64], node_budgets) == ((), (64,))
    assert benchmark.resolve_budgets([25], [64], node_budgets) == ((25,), (64,))


@pytest.mark.parametrize(
    ("option", "value", "expected_kind"),
    [
        ("--time-budget-ms", "25", "time_ms"),
        ("--node-budget", "64", "nodes"),
    ],
)
def test_single_budget_family_override_does_not_append_other_defaults(
    option: str, value: str, expected_kind: str
) -> None:
    completed = subprocess.run(
        [
            sys.executable,
            "-m",
            "benchmarks.bench_issue_43_search_budgets",
            "--repeats",
            "1",
            "--position",
            "0188bbac-t288",
            option,
            value,
            "--json",
        ],
        check=False,
        capture_output=True,
        text=True,
    )

    assert completed.returncode == 0, completed.stderr
    assert completed.stderr == ""
    rows = [json.loads(line) for line in completed.stdout.splitlines()]
    assert len(rows) == 1
    assert rows[0]["budget_kind"] == expected_kind
    assert rows[0]["budget_value"] == int(value)


def test_repeated_node_rows_have_identical_decision_fields() -> None:
    positions, _ = benchmark.load_fixture()
    rows = benchmark.run_matrix(
        positions=positions[:1],
        time_budgets=(),
        node_budgets=(512,),
        repeats=2,
    )

    assert len(rows) == 2
    assert {row["repeat"] for row in rows} == {1, 2}
    stable_fields = {
        key
        for key in rows[0]
        if key not in {"repeat", "elapsed_ms", "root_analysis_elapsed_ms"}
    }
    assert {key: rows[0][key] for key in stable_fields} == {
        key: rows[1][key] for key in stable_fields
    }


def test_wall_row_reports_requested_budget_without_latency_assumption() -> None:
    positions, _ = benchmark.load_fixture()
    [row] = benchmark.run_matrix(
        positions=positions[:1],
        time_budgets=(25,),
        node_budgets=(),
        repeats=1,
    )

    assert row["budget_kind"] == "time_ms"
    assert row["budget_value"] == 25
    assert row["elapsed_ms"] >= 0.0
    assert row["completed_depth"] >= 0
    assert row["max_depth_started"] >= row["completed_depth"]
    if row["minimax_score"] is None:
        assert row["minimax_outcome"] is None
        assert row["minimax_bound"] is None
    else:
        assert row["minimax_outcome"] in {"win", "draw", "unresolved", "loss"}
        assert row["minimax_bound"] in {"exact", "lower", "upper"}


@pytest.mark.parametrize(
    ("option", "value", "message"),
    [
        ("--time-budget-ms", "0", "between 1 and 2147483647"),
        ("--time-budget-ms", "2147483648", "between 1 and 2147483647"),
        ("--node-budget", "-1", "between 1 and 18446744073709551615"),
        (
            "--node-budget",
            "18446744073709551616",
            "between 1 and 18446744073709551615",
        ),
        ("--repeats", "0", "between 1 and 2147483647"),
        ("--repeats", "2147483648", "between 1 and 2147483647"),
    ],
)
def test_cli_rejects_out_of_range_integers_without_traceback(
    option: str, value: str, message: str
) -> None:
    completed = subprocess.run(
        [
            sys.executable,
            "-m",
            "benchmarks.bench_issue_43_search_budgets",
            option,
            value,
            "--json",
        ],
        check=False,
        capture_output=True,
        text=True,
    )

    assert completed.returncode == 2
    assert completed.stdout == ""
    assert "usage:" in completed.stderr
    assert message in completed.stderr
    assert "Traceback" not in completed.stderr


def test_human_table_keeps_selected_score_and_root_latency(
    capsys: pytest.CaptureFixture[str],
) -> None:
    positions, _ = benchmark.load_fixture()
    rows = benchmark.run_matrix(
        positions=positions[:1],
        time_budgets=(),
        node_budgets=(1,),
        repeats=1,
    )

    benchmark._print_table(rows)

    header = capsys.readouterr().out.splitlines()[0].split("\t")
    assert "minimax_score" in header
    assert "root_analysis_elapsed_ms" in header
