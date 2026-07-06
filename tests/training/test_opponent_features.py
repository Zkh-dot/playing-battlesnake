from __future__ import annotations

import unittest

from battlesnake.battlesnake_native import Board, Coord, Snake
from battlesnake.training.opponent_model.features import candidate_rows
from battlesnake.training.opponent_model.schema import MoveObservation


class OpponentFeatureTests(unittest.TestCase):
    def _observation(self, snake_rank: int | None = 3) -> MoveObservation:
        return MoveObservation(
            observation_id="g1:0:s1",
            game_id="g1",
            split="train",
            turn=0,
            snake_id="s1",
            snake_name="Alpha",
            snake_rank=snake_rank,
            target_move="right",
            board_width=7,
            board_height=7,
            alive_snakes=2,
        )

    def test_candidate_rows_emit_four_moves_and_labels(self) -> None:
        board = Board(
            width=7,
            height=7,
            snakes={
                "s1": Snake("s1", "Alpha", 80, [Coord(1, 1), Coord(1, 0)], length=2),
                "s2": Snake("s2", "Beta", 90, [Coord(5, 5), Coord(5, 6)], length=2),
            },
            food=[Coord(2, 1), Coord(6, 6)],
            hazards=[],
            ruleset_name="standard",
            hazard_damage=15,
        )
        observation = self._observation()

        rows = list(candidate_rows(observation, board))

        self.assertEqual([row.candidate_move for row in rows], ["up", "down", "left", "right"])
        self.assertEqual([row.label for row in rows], [0, 0, 0, 1])
        right = rows[3].features
        self.assertEqual(right["candidate_move"], "right")
        self.assertEqual(right["candidate_is_safe"], 1.0)
        self.assertEqual(right["candidate_to_nearest_food"], 0.0)
        self.assertEqual(right["snake_health"], 80.0)

    def test_stable_feature_semantics_for_model_inputs(self) -> None:
        board = Board(
            width=7,
            height=7,
            snakes={
                "s1": Snake(
                    "s1",
                    "Alpha",
                    80,
                    [Coord(2, 2), Coord(2, 1), Coord(2, 0), Coord(1, 0)],
                    length=4,
                ),
                "s2": Snake(
                    "s2",
                    "Equal",
                    90,
                    [Coord(2, 4), Coord(3, 4), Coord(4, 4), Coord(5, 4)],
                    length=4,
                ),
                "s3": Snake(
                    "s3",
                    "Longer",
                    90,
                    [Coord(1, 3), Coord(1, 4), Coord(1, 5), Coord(0, 5), Coord(0, 4)],
                    length=5,
                ),
                "s4": Snake(
                    "s4",
                    "Shorter",
                    90,
                    [Coord(3, 3), Coord(4, 3), Coord(5, 3)],
                    length=3,
                ),
            },
            food=[],
            hazards=[],
            ruleset_name="standard",
            hazard_damage=15,
        )

        rank_zero_rows = {row.candidate_move: row for row in candidate_rows(self._observation(snake_rank=0), board)}
        rank_none_rows = {row.candidate_move: row for row in candidate_rows(self._observation(snake_rank=None), board)}

        up_features = rank_zero_rows["up"].features
        self.assertEqual(up_features["snake_rank"], 0.0)
        self.assertEqual(rank_none_rows["up"].features["snake_rank"], 999.0)
        self.assertEqual(up_features["candidate_to_nearest_food"], 99.0)
        self.assertEqual(up_features["head_to_nearest_food"], 99.0)
        self.assertEqual(up_features["adjacent_longer_or_equal_heads"], 2.0)
        self.assertEqual(up_features["adjacent_shorter_heads"], 1.0)

        down_features = rank_zero_rows["down"].features
        self.assertEqual(down_features["candidate_is_safe"], 0.0)
        self.assertEqual(down_features["candidate_reachable_space"], 0.0)


if __name__ == "__main__":
    unittest.main()
