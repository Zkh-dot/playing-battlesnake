from __future__ import annotations

import unittest

from battlesnake.battlesnake_native import board_hash
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


if __name__ == "__main__":
    unittest.main()
