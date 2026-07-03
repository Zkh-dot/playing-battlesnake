from __future__ import annotations

import unittest

from benchmarks.scenarios import SCENARIOS, build_board


EXPECTED_SCENARIO_NAMES = [
    "duel_open_7x7",
    "duel_center_pressure_11x11",
    "duel_low_health_food_race",
    "duel_tail_chase_trap",
    "duel_corridor_choke",
    "duel_late_game_long_bodies",
    "royale_hazard_ring_duel",
    "standard_four_snakes_dense",
]


def _distance(left, right) -> int:
    return abs(left.x - right.x) + abs(left.y - right.y)


class BenchmarkScenarioTests(unittest.TestCase):
    def test_scenario_fixture_invariants(self) -> None:
        self.assertEqual([scenario.name for scenario in SCENARIOS], EXPECTED_SCENARIO_NAMES)

        for scenario in SCENARIOS:
            with self.subTest(scenario=scenario.name):
                snake_ids = [snake.id for snake in scenario.snakes]
                self.assertEqual(len(snake_ids), len(set(snake_ids)))

                occupied = set()
                for snake in scenario.snakes:
                    self.assertGreater(snake.health, 0)
                    self.assertGreater(len(snake.body), 0)
                    for left, right in zip(snake.body, snake.body[1:]):
                        self.assertEqual(_distance(left, right), 1)
                    for coord in snake.body:
                        self.assertGreaterEqual(coord.x, 0)
                        self.assertLess(coord.x, scenario.width)
                        self.assertGreaterEqual(coord.y, 0)
                        self.assertLess(coord.y, scenario.height)
                        key = (coord.x, coord.y)
                        self.assertNotIn(key, occupied)
                        occupied.add(key)

                for coord in (*scenario.food, *scenario.hazards):
                    self.assertGreaterEqual(coord.x, 0)
                    self.assertLess(coord.x, scenario.width)
                    self.assertGreaterEqual(coord.y, 0)
                    self.assertLess(coord.y, scenario.height)

    def test_every_scenario_builds_with_controlled_snake_head_occupied(self) -> None:
        for scenario in SCENARIOS:
            with self.subTest(scenario=scenario.name):
                board = build_board(scenario)

                self.assertIn(scenario.snake_id, board.snakes)
                self.assertGreater(len(board.snakes[scenario.snake_id].body), 0)
                self.assertGreater(board.snakes[scenario.snake_id].health, 0)
                self.assertIn(
                    board.head(scenario.snake_id),
                    board.occupied(include_tails=True),
                )

    def test_every_scenario_has_safe_move(self) -> None:
        for scenario in SCENARIOS:
            with self.subTest(scenario=scenario.name):
                board = build_board(scenario)

                self.assertGreater(len(board.safe_moves(scenario.snake_id)), 0)


if __name__ == "__main__":
    unittest.main()
