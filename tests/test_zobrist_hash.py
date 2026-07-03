from __future__ import annotations

import unittest

from battlesnake.battlesnake_native import Board, Coord, Snake, board_hash
from benchmarks.scenarios import build_board, get_scenario


class ZobristHashTests(unittest.TestCase):
    def test_same_board_has_same_hash(self) -> None:
        scenario = get_scenario("duel_center_pressure_11x11")
        first = board_hash(build_board(scenario))
        second = board_hash(build_board(scenario))
        self.assertEqual(first, second)

    def test_different_positions_change_hash(self) -> None:
        scenario = get_scenario("duel_open_7x7")
        board = build_board(scenario)
        moved = board.clone_and_apply({"me": "up", "you": "up"})
        self.assertNotEqual(board_hash(board), board_hash(moved))

    def test_food_and_hazard_order_do_not_change_hash(self) -> None:
        snake = Snake(
            id="me",
            name="me",
            health=90,
            body=[Coord(2, 2), Coord(2, 1), Coord(2, 0)],
            length=3,
        )
        first = Board(
            width=7,
            height=7,
            snakes={"me": snake},
            food=[Coord(1, 1), Coord(5, 5)],
            hazards=[Coord(0, 0), Coord(6, 6)],
            ruleset_name="royale",
            hazard_damage=15,
        )
        second = Board(
            width=7,
            height=7,
            snakes={"me": snake},
            food=[Coord(5, 5), Coord(1, 1)],
            hazards=[Coord(6, 6), Coord(0, 0)],
            ruleset_name="royale",
            hazard_damage=15,
        )

        self.assertEqual(board_hash(first), board_hash(second))

    def test_hash_changes_for_state_that_affects_search(self) -> None:
        scenario = get_scenario("duel_open_7x7")
        base = build_board(scenario)
        low_health_snakes = []
        for snake in scenario.snakes:
            health = snake.health - 1 if snake.id == scenario.snake_id else snake.health
            low_health_snakes.append(
                Snake(id=snake.id, name=snake.name, health=health, body=snake.body, length=snake.length)
            )
        low_health = Board(
            width=scenario.width,
            height=scenario.height,
            snakes={snake.id: snake for snake in low_health_snakes},
            food=scenario.food,
            hazards=scenario.hazards,
            ruleset_name=scenario.ruleset_name,
            hazard_damage=scenario.hazard_damage,
        )
        different_ruleset = Board(
            width=scenario.width,
            height=scenario.height,
            snakes={snake.id: snake for snake in scenario.snakes},
            food=scenario.food,
            hazards=scenario.hazards,
            ruleset_name="royale",
            hazard_damage=scenario.hazard_damage,
        )
        different_hazard_damage = Board(
            width=scenario.width,
            height=scenario.height,
            snakes={snake.id: snake for snake in scenario.snakes},
            food=scenario.food,
            hazards=scenario.hazards,
            ruleset_name=scenario.ruleset_name,
            hazard_damage=15,
        )

        self.assertNotEqual(board_hash(base), board_hash(low_health))
        self.assertNotEqual(board_hash(base), board_hash(different_ruleset))
        self.assertNotEqual(board_hash(base), board_hash(different_hazard_damage))

    def test_board_hash_returns_unsigned_64_bit_value(self) -> None:
        scenario = get_scenario("duel_open_7x7")
        value = board_hash(build_board(scenario))

        self.assertIsInstance(value, int)
        self.assertGreaterEqual(value, 0)
        self.assertLessEqual(value, 2**64 - 1)


if __name__ == "__main__":
    unittest.main()
