from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

from battlesnake.strategies.standard import DEFAULT_STANDARD_THETA
from tools.tuning.search_standard_ffa_weights import (
    THETA_SEARCH_SPACE,
    scenario_suite_passes,
    suggest_theta,
)


def test_search_space_covers_standard_theta_keys() -> None:
    expected = {
        "w_expected",
        "w_worst",
        "w_space_log",
        "w_space_ratio",
        "w_escape",
        "w_zero_escape",
        "w_losing_h2h",
        "w_winning_h2h",
        "w_food_on_cell",
        "w_food_route",
        "w_contested_food",
        "w_pocket",
        "territory_delta",
        "opponent_safe_moves",
        "nearby_opponent_distance",
    }

    assert expected <= set(THETA_SEARCH_SPACE)


def test_random_theta_stays_within_bounds_and_suite_accepts_default() -> None:
    theta = suggest_theta(123)

    for key, value in theta.items():
        if key not in THETA_SEARCH_SPACE:
            continue
        lower, upper = THETA_SEARCH_SPACE[key]
        assert lower <= value <= upper
    assert scenario_suite_passes(DEFAULT_STANDARD_THETA)


def test_weight_search_cli_writes_best_theta_and_trials(tmp_path: Path) -> None:
    output = tmp_path / "best.json"
    trials = tmp_path / "trials.jsonl"

    completed = subprocess.run(
        [
            sys.executable,
            "-m",
            "tools.tuning.search_standard_ffa_weights",
            "--trials",
            "2",
            "--games",
            "1",
            "--max-turns",
            "20",
            "--seed",
            "5",
            "--output",
            str(output),
            "--trials-output",
            str(trials),
        ],
        text=True,
        capture_output=True,
        check=True,
    )

    best = json.loads(output.read_text())
    trial_rows = [json.loads(line) for line in trials.read_text().splitlines()]
    summary = json.loads(completed.stdout)

    assert set(DEFAULT_STANDARD_THETA) <= set(best)
    assert len(trial_rows) == 2
    assert trial_rows[0]["trial"] == 0
    assert trial_rows[0]["theta"] == DEFAULT_STANDARD_THETA
    assert summary["output"] == str(output)
