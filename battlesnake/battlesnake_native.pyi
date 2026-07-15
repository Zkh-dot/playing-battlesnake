from __future__ import annotations

from typing import Iterable, Literal, TypedDict


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

class RootCandidateDiagnostics(TypedDict):
    evaluated: bool
    allowed: bool
    rejection_reason: Literal[
        "none", "no_surviving_reply", "proven_short_self_trap", "structurally_dominated"
    ]
    safe_by_board_rules: bool
    reply_outcomes: dict[str, str]
    alive_reply_mask: int
    alive_reply_count: int
    draw_reply_mask: int
    immediate_causes: list[str]
    trap_status: str
    trap_horizon: int
    structural_proof: str
    proof_cutoff: str
    proof_horizon: int
    explored_states: int
    structural_capacity: int
    opponent_closure_considered: bool
    post_move_length: int
    relaxed_static_capacity: int
    refutation_status: str
    minimax_score: float | None
    minimax_outcome: str | None
    minimax_terminal_distance: int | None
    minimax_cause: list[str] | None
    minimax_bound: str | None

class CorridorMetricsDiagnostics(TypedDict):
    immediate_exits: int | None
    forced_steps: int | None
    reachable: int | None

class CorridorGuardCandidateDiagnostics(TypedDict):
    move: str | None
    corridor_metrics: CorridorMetricsDiagnostics
    structural_proof: str | None
    relaxed_static_capacity: int | None
    post_move_length: int | None
    minimax_score: float | None
    minimax_outcome: str | None
    minimax_bound: str | None

class CorridorGuardDiagnostics(TypedDict):
    considered: bool
    incumbent: CorridorGuardCandidateDiagnostics
    proposal: CorridorGuardCandidateDiagnostics
    comparison_ordering: Literal["incumbent", "equal", "candidate", "incomparable"]
    comparison_reason: str
    exact_tie_permitted: bool
    applied: bool
    decision: Literal[
        "not_considered",
        "same_as_incumbent",
        "rejected_search_order",
        "applied_exact_tie",
    ]

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
    node_budget: int
    node_budget_exhausted: bool
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
    root_candidates: dict[str, RootCandidateDiagnostics]
    root_allowed_mask: int
    root_policy_applied: str
    selection_reason: str
    root_comparison_reason: str
    root_analysis_nodes: int
    root_analysis_elapsed_ms: float
    root_analysis_budget_ms: int
    # Scheduled search interval; a noninterruptible leaf may still complete no depth.
    search_reserved_ms: int
    corridor_guard: CorridorGuardDiagnostics

class DuelWeightProfile(TypedDict):
    schema_version: int
    name: str
    version: str
    status: Literal["production-default", "candidate"]
    sha256: str
    weights: dict[str, float]

def reachable_space(board: Board, start: Coord, snake_id: str) -> int: ...
def shortest_path(board: Board, start: Coord, goal: Coord, snake_id: str) -> list[Coord]: ...
def voronoi_territory(board: Board) -> dict[str, set[Coord]]: ...
def space_time_metrics(board: Board, snake_id: str) -> dict[str, int | bool]: ...
def minimax_move(
    board: Board,
    snake_id: str,
    time_budget_ms: int = 400,
    weights: dict[str, float] | None = None,
    root_policy: str = "standard_ladder_opportunity",
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
    root_policy: str = "standard_ladder_opportunity",
    node_budget: int = 0,
) -> MinimaxDiagnostics: ...
def duel_root_profile(board: Board, snake_id: str) -> dict[str, RootCandidateDiagnostics]: ...
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
def duel_weight_profiles() -> list[DuelWeightProfile]: ...
def board_hash(board: Board) -> int: ...
