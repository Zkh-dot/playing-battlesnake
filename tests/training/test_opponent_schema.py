from __future__ import annotations

import unittest

from battlesnake.training.opponent_model.schema import (
    MOVES,
    CandidateRow,
    MoveObservation,
    deterministic_split,
    infer_move,
)


class OpponentSchemaTests(unittest.TestCase):
    def test_infer_move_from_head_delta(self) -> None:
        self.assertEqual(infer_move((2, 2), (2, 3)), "up")
        self.assertEqual(infer_move((2, 2), (2, 1)), "down")
        self.assertEqual(infer_move((2, 2), (1, 2)), "left")
        self.assertEqual(infer_move((2, 2), (3, 2)), "right")
        self.assertIsNone(infer_move((2, 2), (4, 2)))

    def test_split_is_stable_and_uses_three_buckets(self) -> None:
        split = deterministic_split("game-a")
        self.assertEqual(split, deterministic_split("game-a"))
        self.assertIn(split, {"train", "validation", "test"})

    def test_dataclasses_expose_training_keys(self) -> None:
        observation = MoveObservation(
            observation_id="g1:7:s1",
            game_id="g1",
            split="train",
            turn=7,
            snake_id="s1",
            snake_name="Alpha",
            snake_rank=12,
            target_move="right",
            board_width=11,
            board_height=11,
            alive_snakes=4,
        )
        row = CandidateRow(
            observation_id=observation.observation_id,
            game_id=observation.game_id,
            split=observation.split,
            turn=observation.turn,
            snake_id=observation.snake_id,
            snake_name=observation.snake_name,
            snake_rank=observation.snake_rank,
            candidate_move="right",
            label=1,
            features={"candidate_move": "right", "turn": 7.0},
        )
        self.assertEqual(MOVES, ("up", "down", "left", "right"))
        self.assertEqual(row.label, 1)
        self.assertEqual(row.features["candidate_move"], "right")


if __name__ == "__main__":
    unittest.main()
