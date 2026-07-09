from __future__ import annotations

from typing import Iterable, TypedDict


class Coord:
    x: int
    y: int

    def __init__(self, x: int = 0, y: int = 0) -> None: ...


class Snake:
    id: str
    name: str
    health: int
    body: list[Coord]
    head: Coord | None
    length: int

    def __init__(
        self,
        id: str = "",
        name: str = "",
        health: int = 100,
        body: Iterable[Coord] | None = None,
        head: Coord | None = None,
        length: int | None = None,
    ) -> None: ...


class Board:
    width: int
    height: int
    snakes: dict[str, Snake]
    food: set[Coord]
    hazards: set[Coord]
    ruleset_name: str
    hazard_damage: int

    def __init__(
        self,
        width: int,
        height: int,
        snakes: dict[str, Snake] | Iterable[Snake],
        food: Iterable[Coord] | None = None,
        hazards: Iterable[Coord] | None = None,
        ruleset_name: str = "standard",
        hazard_damage: int = 15,
    ) -> None: ...
    def in_bounds(self, coord: Coord) -> bool: ...
    def head(self, snake_id: str) -> Coord: ...
    def step(self, coord: Coord, move: str) -> Coord: ...
    def is_safe(self, coord: Coord, snake_id: str) -> bool: ...
    def safe_moves(self, snake_id: str) -> list[str]: ...
    def occupied(self, include_tails: bool = True) -> set[Coord]: ...
    def clone_and_apply(self, moves: dict[str, str]) -> Board: ...


UP: str
DOWN: str
LEFT: str
RIGHT: str

class MinimaxDiagnostics(TypedDict):
    move: str
    score: float
    root_move_scores: dict[str, float]
    elapsed_ms: float
    parallel_mode: int
    parallel_workers_used: int
    completed_depth: int
    max_depth_started: int
    timed_out: bool
    nodes: int
    leaf_evals: int
    clone_calls: int
    board_allocations: int
    safe_move_calls: int
    beta_cutoffs: int
    move_order_first_choice_cutoffs: int
    tt_probes: int
    tt_hits: int
    tt_exact_hits: int
    tt_lower_hits: int
    tt_upper_hits: int
    tt_cutoffs: int
    tt_stores: int
    tt_collisions: int

def reachable_space(board: Board, start: Coord, snake_id: str) -> int: ...
def shortest_path(board: Board, start: Coord, goal: Coord, snake_id: str) -> list[Coord]: ...
def voronoi_territory(board: Board) -> dict[str, set[Coord]]: ...
def space_time_metrics(board: Board, snake_id: str) -> dict[str, int | bool]: ...
def minimax_move(
    board: Board,
    snake_id: str,
    time_budget_ms: int = 400,
    weights: dict[str, float] | None = None,
) -> str: ...
def minimax_diagnostics(
    board: Board,
    snake_id: str,
    time_budget_ms: int = 400,
    fixed_depth: int = 0,
    enable_tt: bool = True,
    enable_move_ordering: bool = True,
    enable_make_unmake: bool = True,
    weights: dict[str, float] | None = None,
    parallel_mode: str = "serial",
) -> MinimaxDiagnostics: ...
def standard_ffa_move(
    board: Board,
    snake_id: str,
    time_budget_ms: int = 80,
    theta: dict[str, float] | None = None,
) -> str: ...
def choke_points(board: Board, snake_id: str) -> set[Coord]: ...
def edge_trap_move(board: Board, snake_id: str) -> str | None: ...
def predict_hazards(board: Board, turns_ahead: int = 3) -> set[Coord]: ...
def evaluate(board: Board, snake_id: str, weights: dict[str, float] | None = None) -> float: ...
def board_hash(board: Board) -> int: ...
