from __future__ import annotations

import unittest

from battlesnake.main import select_strategy
from battlesnake.strategies.duel import StrategyDuel
from battlesnake.strategies.standard import StrategyStandard
from battlesnake.types import BoardState, Coord, Game, GameState, Ruleset, Snake


def make_state(ruleset_name: str, snake_count: int) -> GameState:
    snakes = [
        Snake(
            id=f"snake-{index}",
            name=f"snake-{index}",
            health=100,
            body=[Coord(x=index, y=1), Coord(x=index, y=0)],
        )
        for index in range(snake_count)
    ]
    return GameState(
        game=Game(id="test-game", ruleset=Ruleset(name=ruleset_name)),
        turn=1,
        board=BoardState(height=7, width=7, food=[], hazards=[], snakes=snakes),
        you=snakes[0],
    )


class StrategySelectionTests(unittest.TestCase):
    def test_standard_two_snake_ladder_duel_uses_duel_strategy(self) -> None:
        self.assertIsInstance(select_strategy(make_state("standard", 2)), StrategyDuel)

    def test_solo_two_snake_duel_uses_duel_strategy(self) -> None:
        self.assertIsInstance(select_strategy(make_state("solo", 2)), StrategyDuel)

    def test_standard_multi_snake_game_uses_standard_strategy(self) -> None:
        self.assertIsInstance(select_strategy(make_state("standard", 3)), StrategyStandard)


if __name__ == "__main__":
    unittest.main()
