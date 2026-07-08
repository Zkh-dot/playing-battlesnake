from __future__ import annotations

import unittest
from unittest.mock import patch

import battlesnake.main as main_module
from battlesnake.main import select_strategy
from battlesnake.strategies.duel import StrategyDuel
from battlesnake.strategies.first_safe import StrategyFirstSafe
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

    def test_standard_multi_snake_game_uses_default_variant(self) -> None:
        self.assertIsInstance(select_strategy(make_state("standard", 3)), StrategyFirstSafe)

    def test_standard_v1_variant_uses_tuned_theta(self) -> None:
        with patch.dict("os.environ", {"STRATEGY_VARIANT": "standard-v1"}):
            strategy = select_strategy(make_state("standard", 3))

        self.assertIsInstance(strategy, StrategyStandard)
        self.assertEqual(strategy.opponent_prior, "model")
        self.assertAlmostEqual(strategy.theta["w_pocket"], 606.598196752109)
        self.assertAlmostEqual(strategy.theta["w_food_on_cell"], 299.49531231126576)

    def test_standard_v1_late_env_change_preloads_model_prior_once(self) -> None:
        calls = []

        def fake_preload(self) -> bool:
            calls.append(self)
            return True

        with (
            patch.dict("os.environ", {"STRATEGY_VARIANT": "standard-v1"}),
            patch.object(main_module, "_standard_prior_preload_attempted", False),
            patch("battlesnake.opponent_model_prior.LightGBMOpponentPrior.preload", fake_preload),
        ):
            self.assertIsInstance(select_strategy(make_state("standard", 3)), StrategyStandard)
            self.assertIsInstance(select_strategy(make_state("standard", 3)), StrategyStandard)

        self.assertEqual(len(calls), 1)


if __name__ == "__main__":
    unittest.main()
