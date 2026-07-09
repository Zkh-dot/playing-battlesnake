from __future__ import annotations

import unittest

from battlesnake.battlesnake_native import Board, Coord, Snake, evaluate, space_time_metrics


TERMINAL_LOSS = -1000000.0
SURVIVAL_STEP = 1000.0
LOSS_BAND_CEILING = TERMINAL_LOSS + SURVIVAL_STEP * 33


def _board(width: int, height: int, snakes: list[Snake], food: list[Coord] | None = None) -> Board:
    return Board(
        width=width,
        height=height,
        snakes={snake.id: snake for snake in snakes},
        food=food or [],
        hazards=[],
        ruleset_name="standard",
        hazard_damage=0,
    )


def _snake(snake_id: str, body: list[tuple[int, int]], health: int = 90) -> Snake:
    coords = [Coord(x, y) for x, y in body]
    return Snake(snake_id, snake_id, health, coords, coords[0], len(coords))


class SpaceTimeMetricsTests(unittest.TestCase):
    def test_open_board_is_alive(self) -> None:
        board = _board(
            7,
            7,
            [
                _snake("me", [(3, 3), (3, 2), (3, 1)]),
                _snake("you", [(6, 6), (6, 5), (6, 4)]),
            ],
        )

        metrics = space_time_metrics(board, "me")

        self.assertFalse(metrics["dead"])
        self.assertTrue(metrics["tail_reachable"])
        self.assertGreater(metrics["reachable_cells"], 20)

    def test_sealed_self_pocket_is_dead(self) -> None:
        # Head at (1,0); (0,0) is the only free neighbor; every wall segment
        # vacates far later than we can arrive. Region = 1 cell < length 13.
        me = _snake(
            "me",
            [
                (1, 0), (2, 0), (2, 1), (1, 1), (0, 1), (0, 2), (1, 2),
                (2, 2), (3, 2), (4, 2), (4, 1), (4, 0), (5, 0),
            ],
        )
        board = _board(11, 11, [me, _snake("you", [(9, 9), (9, 8), (9, 7)])])

        metrics = space_time_metrics(board, "me")

        self.assertTrue(metrics["dead"])
        self.assertFalse(metrics["tail_reachable"])
        self.assertEqual(metrics["reachable_cells"], 1)
        self.assertEqual(metrics["max_arrival"], 1)

    def test_short_snake_pocket_opens_in_time(self) -> None:
        # Same geometry but the snake is short enough that the wall vacates
        # before we run out of cells: tail chase keeps us alive.
        me = _snake("me", [(1, 0), (2, 0), (2, 1), (1, 1), (0, 1), (0, 2)])
        board = _board(11, 11, [me, _snake("you", [(9, 9), (9, 8), (9, 7)])])

        metrics = space_time_metrics(board, "me")

        self.assertFalse(metrics["dead"])
        self.assertTrue(metrics["tail_reachable"])

    def test_growth_delays_tail_vacation(self) -> None:
        # Food under our tail delays vacation by one turn; the alive-pocket
        # case above must report strictly fewer reachable cells with food.
        body = [(1, 0), (2, 0), (2, 1), (1, 1), (0, 1), (0, 2)]
        me = _snake("me", body)
        opponent = _snake("you", [(9, 9), (9, 8), (9, 7)])
        plain = space_time_metrics(_board(11, 11, [me, opponent]), "me")
        fed = space_time_metrics(
            _board(11, 11, [me, opponent], food=[Coord(0, 2)]),
            "me",
        )

        self.assertLess(fed["reachable_cells"], plain["reachable_cells"])

    def test_revisits_open_area_until_body_path_vacates(self) -> None:
        me = _snake(
            "me",
            [(0, 2), (0, 1), (1, 1), (2, 1), (3, 1), (3, 0), (2, 0), (1, 0), (0, 0)],
        )
        board = _board(4, 4, [me])

        metrics = space_time_metrics(board, "me")

        self.assertFalse(metrics["dead"])
        self.assertTrue(metrics["tail_reachable"])

    def test_longer_opponent_claims_contested_cells(self) -> None:
        # An equal-or-longer opponent close to open space shrinks our region;
        # a shorter opponent in the same spot is ignored by the contest.
        me = _snake("me", [(0, 3), (0, 2), (0, 1)])
        long_opponent = _snake("you", [(2, 3), (3, 3), (4, 3), (5, 3)])
        short_opponent = _snake("you", [(2, 3), (3, 3)])

        contested = space_time_metrics(_board(7, 7, [me, long_opponent]), "me")
        free = space_time_metrics(_board(7, 7, [me, short_opponent]), "me")

        self.assertLess(contested["reachable_cells"], free["reachable_cells"])


class DeadRegionEvaluationTests(unittest.TestCase):
    def _dead_pocket_board(self) -> Board:
        me = _snake(
            "me",
            [
                (1, 0), (2, 0), (2, 1), (1, 1), (0, 1), (0, 2), (1, 2),
                (2, 2), (3, 2), (4, 2), (4, 1), (4, 0), (5, 0),
            ],
        )
        return _board(11, 11, [me, _snake("you", [(9, 9), (9, 8), (9, 7)])])

    def test_dead_pocket_scores_in_terminal_loss_band(self) -> None:
        score = evaluate(self._dead_pocket_board(), "me")

        self.assertLessEqual(score, LOSS_BAND_CEILING)
        self.assertGreater(score, TERMINAL_LOSS)
        # max_arrival is 1 for this pocket -> exactly one survival step.
        self.assertEqual(score, TERMINAL_LOSS + SURVIVAL_STEP * 1)

    def test_alive_pocket_scores_normally(self) -> None:
        me = _snake("me", [(1, 0), (2, 0), (2, 1), (1, 1), (0, 1), (0, 2)])
        board = _board(11, 11, [me, _snake("you", [(9, 9), (9, 8), (9, 7)])])

        score = evaluate(board, "me")

        self.assertGreater(score, LOSS_BAND_CEILING)

    def test_deeper_pocket_outranks_shallow_pocket(self) -> None:
        # A dead region with more survivable turns must score strictly higher,
        # so minimax prefers the longer survival line among forced losses.
        shallow = evaluate(self._dead_pocket_board(), "me")
        me = _snake("me", [(6, 2), (6, 1), (5, 1), (5, 2), (4, 2)])
        opponent = _snake("you", [(4, 3), (4, 4), (3, 4), (3, 3), (2, 3)])
        deeper_board = _board(7, 7, [me, opponent])
        deeper_metrics = space_time_metrics(deeper_board, "me")
        deeper = evaluate(deeper_board, "me")

        self.assertTrue(deeper_metrics["dead"])
        self.assertGreater(deeper_metrics["max_arrival"], 1)
        self.assertLess(shallow, deeper)


if __name__ == "__main__":
    unittest.main()
