from __future__ import annotations

import unittest

from battlesnake.battlesnake_native import Board, Coord, Snake
from battlesnake.training.opponent_model.features import candidate_rows
from battlesnake.training.opponent_model.schema import MoveObservation


class OpponentFeatureTests(unittest.TestCase):
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
        observation = MoveObservation(
            observation_id="g1:0:s1",
            game_id="g1",
            split="train",
            turn=0,
            snake_id="s1",
            snake_name="Alpha",
            snake_rank=3,
            target_move="right",
            board_width=7,
            board_height=7,
            alive_snakes=2,
        )

        rows = list(candidate_rows(observation, board))

        self.assertEqual([row.candidate_move for row in rows], ["up", "down", "left", "right"])
        self.assertEqual([row.label for row in rows], [0, 0, 0, 1])
        right = rows[3].features
        self.assertEqual(right["candidate_move"], "right")
        self.assertEqual(right["candidate_is_safe"], 1.0)
        self.assertEqual(right["candidate_to_nearest_food"], 0.0)
        self.assertEqual(right["snake_health"], 80.0)


if __name__ == "__main__":
    unittest.main()
