from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from tools.tuning.replay_dataset import (
    ReplaySample,
    deterministic_split,
    infer_move,
    iter_replay_samples,
)


def coord(x: int, y: int) -> dict[str, int]:
    return {"X": x, "Y": y}


def snake(snake_id: str, name: str, body: list[tuple[int, int]], health: int = 90) -> dict[str, object]:
    return {
        "ID": snake_id,
        "Name": name,
        "Body": [coord(x, y) for x, y in body],
        "Health": health,
        "Latency": "1",
    }


def synthetic_export() -> dict[str, object]:
    return {
        "game_id": "game-a",
        "game": {
            "ID": "game-a",
            "Width": 7,
            "Height": 7,
            "Ruleset": {"name": "duel", "hazardDamagePerTurn": "15"},
            "RulesetName": "duel",
        },
        "frames": [
            {
                "Turn": 0,
                "Snakes": [
                    snake("s1", "Alpha", [(1, 1), (1, 0)]),
                    snake("s2", "Beta", [(5, 5), (5, 6)]),
                ],
                "Food": [coord(3, 3)],
                "Hazards": [],
            },
            {
                "Turn": 1,
                "Snakes": [
                    snake("s1", "Alpha", [(2, 1), (1, 1)]),
                    snake("s2", "Beta", [(5, 4), (5, 5)]),
                ],
                "Food": [coord(3, 3)],
                "Hazards": [],
            },
        ],
    }


class ReplayDatasetTests(unittest.TestCase):
    def test_infer_move_from_head_delta(self) -> None:
        self.assertEqual(infer_move((1, 1), (1, 2)), "up")
        self.assertEqual(infer_move((1, 1), (1, 0)), "down")
        self.assertEqual(infer_move((1, 1), (0, 1)), "left")
        self.assertEqual(infer_move((1, 1), (2, 1)), "right")
        self.assertIsNone(infer_move((1, 1), (3, 1)))

    def test_iter_replay_samples_builds_native_board_and_target_moves(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "game-a.json"
            path.write_text(json.dumps(synthetic_export()))

            samples = list(iter_replay_samples([path]))

        self.assertEqual(len(samples), 2)
        first = samples[0]
        self.assertIsInstance(first, ReplaySample)
        self.assertEqual(first.game_id, "game-a")
        self.assertEqual(first.turn, 0)
        self.assertEqual(first.snake_id, "s1")
        self.assertEqual(first.snake_name, "Alpha")
        self.assertEqual(first.target_move, "right")
        self.assertEqual(first.board.width, 7)
        self.assertEqual(first.board.height, 7)
        self.assertIn("s1", first.board.snakes)
        self.assertIn("s2", first.board.snakes)

        second = samples[1]
        self.assertEqual(second.snake_id, "s2")
        self.assertEqual(second.target_move, "down")

    def test_deterministic_split_is_stable_by_game_id(self) -> None:
        split_a = deterministic_split("game-a")
        split_a_again = deterministic_split("game-a")
        split_b = deterministic_split("game-b")

        self.assertEqual(split_a, split_a_again)
        self.assertIn(split_a, {"train", "validation", "test"})
        self.assertIn(split_b, {"train", "validation", "test"})


if __name__ == "__main__":
    unittest.main()
