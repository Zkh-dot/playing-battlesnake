from __future__ import annotations

from battlesnake.battlesnake_native import Board, Coord, Snake, reachable_space, voronoi_territory


def c(x: int, y: int) -> Coord:
    return Coord(x, y)


def snake(
    snake_id: str,
    body: list[tuple[int, int]],
    *,
    health: int = 100,
    length: int | None = None,
) -> Snake:
    return Snake(
        id=snake_id,
        name=snake_id,
        health=health,
        body=[c(x, y) for x, y in body],
        length=length,
    )


def point(coord: Coord) -> tuple[int, int]:
    return (coord.x, coord.y)


def points(coords: set[Coord]) -> set[tuple[int, int]]:
    return {point(coord) for coord in coords}


def body_points(native_snake: Snake) -> list[tuple[int, int]]:
    return [point(coord) for coord in native_snake.body]


def test_clone_and_apply_equal_length_head_to_head_removes_both_attackers() -> None:
    board = Board(
        5,
        5,
        [
            snake("alpha", [(1, 2), (1, 1), (1, 0)]),
            snake("bravo", [(3, 2), (3, 1), (3, 0)]),
            snake("charlie", [(4, 0)]),
        ],
    )

    next_board = board.clone_and_apply({"alpha": "right", "bravo": "left", "charlie": "up"})

    assert set(next_board.snakes) == {"charlie"}
    assert point(next_board.snakes["charlie"].head) == (4, 1)


def test_clone_and_apply_unequal_head_to_head_removes_shorter_snake_only() -> None:
    board = Board(
        5,
        5,
        [
            snake("long", [(1, 2), (1, 1), (1, 0), (0, 0)]),
            snake("short", [(3, 2), (3, 1), (3, 0)]),
            snake("bystander", [(4, 0)]),
        ],
    )

    next_board = board.clone_and_apply({"long": "right", "short": "left", "bystander": "up"})

    assert set(next_board.snakes) == {"long", "bystander"}
    assert point(next_board.snakes["long"].head) == (2, 2)


def test_clone_and_apply_body_collision_removes_colliding_snake() -> None:
    board = Board(
        5,
        5,
        [
            snake("crasher", [(2, 2), (2, 1), (2, 0)]),
            snake("wall", [(4, 2), (3, 2), (3, 1)]),
            snake("bystander", [(0, 0)]),
        ],
    )

    next_board = board.clone_and_apply({"crasher": "right", "wall": "up", "bystander": "up"})

    assert set(next_board.snakes) == {"wall", "bystander"}
    assert point(next_board.snakes["wall"].head) == (4, 3)


def test_clone_and_apply_food_growth_health_reset_and_food_removal() -> None:
    board = Board(
        5,
        5,
        [
            snake("hungry", [(1, 1), (1, 0)], health=12),
            snake("other", [(4, 4)]),
            snake("third", [(0, 4)]),
        ],
        food=[c(2, 1), c(4, 0)],
    )

    next_board = board.clone_and_apply({"hungry": "right", "other": "left", "third": "right"})
    hungry = next_board.snakes["hungry"]

    assert hungry.health == 100
    assert hungry.length == 3
    assert body_points(hungry) == [(2, 1), (1, 1), (1, 0)]
    assert points(next_board.food) == {(4, 0)}


def test_clone_and_apply_health_decrement_tail_vacation_and_hazard_damage() -> None:
    board = Board(
        5,
        5,
        [
            snake("mover", [(1, 1), (1, 0)], health=90),
            snake("hazardous", [(3, 1), (3, 0)], health=20),
            snake("other", [(0, 4)]),
        ],
        hazards=[c(4, 1)],
        hazard_damage=15,
    )

    next_board = board.clone_and_apply({"mover": "up", "hazardous": "right", "other": "right"})

    mover = next_board.snakes["mover"]
    hazardous = next_board.snakes["hazardous"]
    assert mover.health == 89
    assert body_points(mover) == [(1, 2), (1, 1)]
    assert hazardous.health == 4
    assert body_points(hazardous) == [(4, 1), (3, 1)]


def test_clone_and_apply_removes_snakes_that_die_from_health_loss() -> None:
    board = Board(
        5,
        5,
        [
            snake("starving", [(1, 1), (1, 0)], health=1),
            snake("survivor", [(3, 3), (3, 2)]),
            snake("other", [(0, 4)]),
        ],
    )

    next_board = board.clone_and_apply({"starving": "up", "survivor": "up", "other": "right"})

    assert set(next_board.snakes) == {"survivor", "other"}


def test_reachable_space_treats_vacating_non_constrictor_tail_as_open() -> None:
    board = Board(
        3,
        3,
        [
            snake("me", [(1, 1), (1, 0), (0, 0)]),
            snake("other", [(2, 2)]),
        ],
    )

    # Current scoring semantics: in non-constrictor games, a tail that is
    # expected to vacate on the next turn is traversable for flood fill.
    assert reachable_space(board, c(0, 0), "me") == 7


def test_reachable_space_blocks_opponent_tail_when_that_snake_can_eat_next_turn() -> None:
    base_snakes = [
        snake("me", [(0, 2), (0, 1), (0, 0)]),
        snake("opponent", [(1, 1), (1, 0)]),
    ]
    without_food = Board(3, 3, base_snakes)
    with_adjacent_food = Board(3, 3, base_snakes, food=[c(2, 1)])

    assert reachable_space(without_food, c(1, 0), "me") > 0
    assert reachable_space(with_adjacent_food, c(1, 0), "me") == 0


def test_voronoi_territory_excludes_tie_cells_on_three_snake_ffa_board() -> None:
    board = Board(
        5,
        5,
        [
            snake("northwest", [(0, 0)]),
            snake("northeast", [(4, 0)]),
            snake("southwest", [(0, 4)]),
        ],
    )

    territory = {snake_id: points(coords) for snake_id, coords in voronoi_territory(board).items()}

    assert territory["northwest"] >= {(0, 0), (1, 0), (0, 1)}
    assert territory["northeast"] >= {(4, 0), (3, 0), (4, 1)}
    assert territory["southwest"] >= {(0, 4), (0, 3), (1, 4)}
    assert (2, 2) not in set().union(*territory.values())


def test_voronoi_territory_handles_four_snakes_dead_snake_and_empty_board() -> None:
    board = Board(
        5,
        5,
        [
            snake("northwest", [(0, 0)]),
            snake("northeast", [(4, 0)]),
            snake("southwest", [(0, 4)]),
            snake("southeast", [(4, 4)]),
            snake("dead", []),
        ],
    )

    territory = {snake_id: points(coords) for snake_id, coords in voronoi_territory(board).items()}

    assert set(territory) == {"northwest", "northeast", "southwest", "southeast", "dead"}
    assert territory["dead"] == set()
    assert (2, 2) not in set().union(*territory.values())
    assert voronoi_territory(Board(5, 5, [])) == {}


def test_safe_moves_blocks_equal_or_longer_head_to_head_cells() -> None:
    board = Board(
        5,
        5,
        [
            snake("me", [(1, 1), (1, 0), (0, 0)]),
            snake("equal", [(3, 1), (3, 0), (4, 0)]),
            snake("other", [(0, 4)]),
        ],
    )

    assert "right" not in board.safe_moves("me")


def test_safe_moves_allows_shorter_head_to_head_cells_and_vacating_tails() -> None:
    shorter_opponent = Board(
        5,
        5,
        [
            snake("me", [(1, 1), (1, 0), (0, 0)]),
            snake("shorter", [(3, 1), (3, 0)]),
            snake("other", [(0, 4)]),
        ],
    )
    own_tail = Board(
        3,
        3,
        [
            snake("me", [(1, 1), (1, 0)]),
            snake("other", [(2, 2)]),
        ],
    )

    assert "right" in shorter_opponent.safe_moves("me")
    assert "down" in own_tail.safe_moves("me")
