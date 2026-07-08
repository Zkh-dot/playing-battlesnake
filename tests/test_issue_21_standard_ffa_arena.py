from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

from tools.standard_ffa_arena import format_summary, run_paired_arena


class Args:
    games = 2
    seed = 11
    max_turns = 30
    min_food = 2
    latency_budget_ms = 80.0


def test_paired_arena_report_contains_objective_metrics() -> None:
    report = run_paired_arena(Args())

    assert report["config"]["games"] == 2
    assert report["config"]["candidate"] == "standard-v1"
    assert report["config"]["baseline"] == "first-safe"
    assert "objective" in report["candidate"]
    assert "placements" in report["candidate"]
    assert "death_causes" in report["candidate"]
    assert "latency_ms" in report["candidate"]
    assert "objective_ci95" in report["candidate"]
    assert "mean_placement_delta" in report["paired"]
    assert isinstance(report["candidate"]["latency_gate_passed"], bool)


def test_summary_is_human_readable() -> None:
    report = run_paired_arena(Args())
    summary = format_summary(report)

    assert "Standard FFA paired arena" in summary
    assert "candidate objective=" in summary
    assert "baseline  objective=" in summary
    assert "candidate placements=" in summary


def test_arena_cli_writes_json_and_summary(tmp_path: Path) -> None:
    output = tmp_path / "arena.json"
    summary = tmp_path / "arena.txt"

    completed = subprocess.run(
        [
            sys.executable,
            "tools/standard_ffa_arena.py",
            "--games",
            "2",
            "--max-turns",
            "30",
            "--seed",
            "17",
            "--output",
            str(output),
            "--summary-output",
            str(summary),
        ],
        text=True,
        capture_output=True,
        check=True,
    )

    report = json.loads(output.read_text())
    assert report["config"]["games"] == 2
    assert report["candidate"]["latency_gate_passed"] is True
    assert "Standard FFA paired arena" in completed.stdout
    assert "paired mean_placement_delta" in summary.read_text()
