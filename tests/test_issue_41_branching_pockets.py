from battlesnake.battlesnake_native import Board, Coord, Snake, minimax_diagnostics


def _board(width: int, height: int, me_body: list[tuple[int, int]], you_body: list[tuple[int, int]]) -> Board:
    return Board(
        width=width,
        height=height,
        snakes={
            "me": Snake("me", "me", 90, [Coord(x, y) for x, y in me_body]),
            "you": Snake("you", "you", 90, [Coord(x, y) for x, y in you_body]),
        },
        food=[],
        hazards=[],
        ruleset_name="standard",
        hazard_damage=0,
    )


def _candidate(board: Board, move: str) -> dict[str, object]:
    result = minimax_diagnostics(board, "me", time_budget_ms=1000, fixed_depth=1)
    return result["root_candidates"][move]


def test_branching_pocket_proves_every_branch_dies() -> None:
    board = _board(
        5,
        5,
        [(2, 0), (2, 1), (2, 2), (1, 2), (0, 2), (0, 3), (1, 3), (1, 4), (0, 4)],
        [(4, 4), (4, 3), (4, 2)],
    )

    candidate = _candidate(board, "left")

    assert candidate["structural_proof"] == "unsafe"
    assert candidate["proof_cutoff"] == "dead_end"
    assert candidate["proof_horizon"] == 9
    assert candidate["explored_states"] > 1


def test_repeatable_loop_is_not_rejected_as_unsafe() -> None:
    board = _board(
        3,
        2,
        [(0, 0), (0, 1), (1, 1), (1, 0)],
        [(2, 1)],
    )

    candidate = _candidate(board, "right")

    assert candidate["structural_proof"] == "safe"
    assert candidate["proof_cutoff"] in {"cycle", "horizon", "capacity"}
    assert candidate["explored_states"] > 1


def test_equal_length_opponent_closes_root_doorway() -> None:
    board = _board(
        7,
        7,
        [(2, 3), (1, 3), (1, 2)],
        [(4, 3), (5, 3), (5, 2)],
    )

    candidate = _candidate(board, "right")

    assert candidate["opponent_closure_considered"] is True
    assert candidate["structural_proof"] != "safe"
