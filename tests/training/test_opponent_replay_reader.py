from __future__ import annotations

import unittest

from battlesnake.training.opponent_model.archive_loader import PlayerMeta
from battlesnake.training.opponent_model.replay_reader import iter_move_observations


def coord(x: int, y: int) -> dict[str, int]:
    return {"X": x, "Y": y}


def snake(snake_id: str, name: str, body: list[tuple[int, int]], health: int = 90) -> dict[str, object]:
    return {
        "ID": snake_id,
        "Name": name,
        "Health": health,
        "Body": [coord(x, y) for x, y in body],
    }


def standard_export() -> dict[str, object]:
    return {
        "game_id": "game-a",
        "game": {
            "ID": "game-a",
            "Width": 11,
            "Height": 11,
            "RulesetName": "standard",
            "Ruleset": {"name": "standard", "hazardDamagePerTurn": "15"},
        },
        "frames": [
            {
                "Turn": 5,
                "Snakes": [
                    snake("s1", "Alpha", [(1, 1), (1, 0)]),
                    snake("s2", "Beta", [(5, 5), (5, 6)]),
                ],
                "Food": [coord(3, 3)],
                "Hazards": [],
            },
            {
                "Turn": 6,
                "Snakes": [
                    snake("s1", "Alpha", [(2, 1), (1, 1)]),
                    snake("s2", "Beta", [(5, 4), (5, 5)]),
                ],
                "Food": [coord(3, 3)],
                "Hazards": [],
            },
        ],
    }


class OpponentReplayReaderTests(unittest.TestCase):
    def test_iter_move_observations_for_standard_game(self) -> None:
        ranks = {"Alpha": PlayerMeta(rank=1, slug="alpha", display="Alpha")}
        observations = list(iter_move_observations("game-a.json", standard_export(), ranks))
        self.assertEqual(len(observations), 2)
        self.assertEqual(observations[0].observation.target_move, "right")
        self.assertEqual(observations[0].observation.snake_rank, 1)
        self.assertEqual(observations[0].board.width, 11)
        self.assertEqual(observations[1].observation.target_move, "down")
        self.assertIsNone(observations[1].observation.snake_rank)

    def test_skips_non_standard_ruleset(self) -> None:
        export = standard_export()
        export["game"]["RulesetName"] = "royale"
        self.assertEqual(list(iter_move_observations("game-a.json", export, {})), [])


if __name__ == "__main__":
    unittest.main()
