from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path


def coord(x: int, y: int) -> dict[str, int]:
    return {"X": x, "Y": y}


def snake(
    snake_id: str,
    name: str,
    body: list[tuple[int, int]],
    health: int = 90,
) -> dict[str, object]:
    return {
        "ID": snake_id,
        "Name": name,
        "Health": health,
        "Body": [coord(x, y) for x, y in body],
    }


def replay(game_id: str) -> dict[str, object]:
    return {
        "game_id": game_id,
        "game": {
            "ID": game_id,
            "Width": 7,
            "Height": 7,
            "RulesetName": "standard",
            "Ruleset": {"name": "standard", "hazardDamagePerTurn": "15"},
        },
        "frames": [
            {
                "Turn": 0,
                "Snakes": [
                    snake("s1", "Alpha", [(1, 1), (1, 0)]),
                    snake("s2", "Beta", [(5, 5), (5, 6)]),
                ],
                "Food": [coord(2, 1)],
                "Hazards": [],
            },
            {
                "Turn": 1,
                "Snakes": [
                    snake("s1", "Alpha", [(2, 1), (1, 1)]),
                    snake("s2", "Beta", [(5, 4), (5, 5)]),
                ],
                "Food": [coord(2, 1)],
                "Hazards": [],
            },
        ],
    }


class OpponentTrainingCliTests(unittest.TestCase):
    def test_cli_builds_dataset_without_training_when_requested(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            archive = Path(tmp) / "games.zip"
            out_dir = Path(tmp) / "out"
            manifest = {
                "selected_players": [
                    {"rank": 1, "slug": "alpha", "display": "Alpha"},
                    {"rank": 2, "slug": "beta", "display": "Beta"},
                ]
            }
            with zipfile.ZipFile(archive, "w") as zf:
                zf.writestr("MANIFEST.json", json.dumps(manifest))
                zf.writestr("game-a.json", json.dumps(replay("game-a")))

            result = subprocess.run(
                [
                    sys.executable,
                    "tools/train_standard_ffa_opponent_model.py",
                    "--archive",
                    str(archive),
                    "--out-dir",
                    str(out_dir),
                    "--csv-chunksize",
                    "4",
                    "--dataset-only",
                ],
                check=True,
                text=True,
                capture_output=True,
            )

            stdout = json.loads(result.stdout)
            self.assertIn("dataset_csv", stdout)
            self.assertTrue((out_dir / "candidate_rows.csv").exists())
            self.assertTrue((out_dir / "dataset_summary.json").exists())

    def test_cli_training_handles_empty_test_split(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            archive = Path(tmp) / "games.zip"
            out_dir = Path(tmp) / "out"
            manifest = {
                "selected_players": [
                    {"rank": 1, "slug": "alpha", "display": "Alpha"},
                    {"rank": 2, "slug": "beta", "display": "Beta"},
                ]
            }
            with zipfile.ZipFile(archive, "w") as zf:
                zf.writestr("MANIFEST.json", json.dumps(manifest))
                zf.writestr("game-3.json", json.dumps(replay("game-3")))
                zf.writestr("game-2.json", json.dumps(replay("game-2")))

            result = subprocess.run(
                [
                    sys.executable,
                    "tools/train_standard_ffa_opponent_model.py",
                    "--archive",
                    str(archive),
                    "--out-dir",
                    str(out_dir),
                    "--max-train-observations",
                    "1",
                    "--csv-chunksize",
                    "4",
                ],
                check=True,
                text=True,
                capture_output=True,
            )

            stdout = json.loads(result.stdout)
            self.assertIn("metrics_json", stdout)
            self.assertIn("model_path", stdout)
            self.assertTrue(Path(stdout["metrics_json"]).exists())
            self.assertTrue(Path(stdout["model_path"]).exists())


if __name__ == "__main__":
    unittest.main()
