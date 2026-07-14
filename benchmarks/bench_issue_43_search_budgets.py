"""Report search-budget diagnostics for the committed issue-43 positions."""

from __future__ import annotations

import argparse
import json
import time
from pathlib import Path
from typing import Any, Iterable, Sequence

from battlesnake.battlesnake_native import Board, Coord, Snake, minimax_diagnostics


FIXTURE_PATH = (
    Path(__file__).parents[1]
    / "tests"
    / "fixtures"
    / "issue_43_search_budget_positions.json"
)
DEFAULT_TIME_BUDGETS = (100, 200, 300)


def load_fixture() -> tuple[list[dict[str, Any]], tuple[int, ...]]:
    fixture = json.loads(FIXTURE_PATH.read_text(encoding="utf-8"))
    return fixture["positions"], tuple(int(value) for value in fixture["node_budgets"])


def case_id(position: dict[str, Any]) -> str:
    evidence = position["evidence"]
    return f'{evidence["game_id"][:8]}-t{evidence["turn"]}'


def _board(position: dict[str, Any]) -> Board:
    return Board(
        width=int(position["width"]),
        height=int(position["height"]),
        snakes={
            str(snake["id"]): Snake(
                str(snake["id"]),
                str(snake["name"]),
                int(snake["health"]),
                [Coord(int(x), int(y)) for x, y in snake["body"]],
            )
            for snake in position["snakes"]
        },
        food=[Coord(int(x), int(y)) for x, y in position["food"]],
        hazards=[Coord(int(x), int(y)) for x, y in position["hazards"]],
        ruleset_name=str(position["ruleset_name"]),
        hazard_damage=int(position["hazard_damage"]),
    )


def _run(
    position: dict[str, Any],
    budget_kind: str,
    budget_value: int,
    repeat: int,
) -> dict[str, Any]:
    arguments = (
        {"time_budget_ms": budget_value}
        if budget_kind == "time_ms"
        else {"node_budget": budget_value}
    )
    started_ns = time.perf_counter_ns()
    diagnostics = minimax_diagnostics(
        _board(position),
        str(position["snake_id"]),
        **arguments,
    )
    elapsed_ms = (time.perf_counter_ns() - started_ns) / 1_000_000.0
    move = str(diagnostics["move"])
    selected = diagnostics["root_candidates"][move]
    evidence = position["evidence"]
    minimax_score = selected["minimax_score"]

    return {
        "case_id": case_id(position),
        "case": str(position["case"]),
        "game_id": str(evidence["game_id"]),
        "turn": int(evidence["turn"]),
        "budget_kind": budget_kind,
        "budget_value": budget_value,
        "repeat": repeat,
        "move": move,
        "structural_proof": str(selected["structural_proof"]),
        "proof_cutoff": str(selected["proof_cutoff"]),
        "completed_depth": int(diagnostics["completed_depth"]),
        "max_depth_started": int(diagnostics["max_depth_started"]),
        "minimax_outcome": selected["minimax_outcome"],
        "minimax_bound": selected["minimax_bound"],
        "minimax_score": float(minimax_score) if minimax_score is not None else None,
        "root_comparison_reason": str(diagnostics["root_comparison_reason"]),
        "selection_reason": str(diagnostics["selection_reason"]),
        "nodes": int(diagnostics["nodes"]),
        "root_analysis_nodes": int(diagnostics["root_analysis_nodes"]),
        "elapsed_ms": elapsed_ms,
        "root_analysis_elapsed_ms": float(diagnostics["root_analysis_elapsed_ms"]),
    }


def run_matrix(
    *,
    positions: Sequence[dict[str, Any]],
    time_budgets: Sequence[int],
    node_budgets: Sequence[int],
    repeats: int,
) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    budgets = (
        [("time_ms", value) for value in time_budgets]
        + [("nodes", value) for value in node_budgets]
    )
    for position in positions:
        for budget_kind, budget_value in budgets:
            for repeat in range(1, repeats + 1):
                rows.append(_run(position, budget_kind, int(budget_value), repeat))
    return rows


def _positive(value: str) -> int:
    parsed = int(value)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("must be a positive integer")
    return parsed


def _unique(
    values: Iterable[int], option: str, parser: argparse.ArgumentParser
) -> tuple[int, ...]:
    result = tuple(values)
    if len(result) != len(set(result)):
        parser.error(f"{option} must not contain duplicates")
    return result


def _print_table(rows: Sequence[dict[str, Any]]) -> None:
    columns = (
        "case_id",
        "budget_kind",
        "budget_value",
        "repeat",
        "move",
        "structural_proof",
        "proof_cutoff",
        "completed_depth",
        "max_depth_started",
        "minimax_outcome",
        "minimax_bound",
        "minimax_score",
        "root_comparison_reason",
        "selection_reason",
        "nodes",
        "root_analysis_nodes",
        "elapsed_ms",
        "root_analysis_elapsed_ms",
    )
    print("\t".join(columns))
    for row in rows:
        print("\t".join(str(row[column]) for column in columns))


def main(argv: Sequence[str] | None = None) -> int:
    positions, fixture_node_budgets = load_fixture()
    parser = argparse.ArgumentParser(
        description="Measure issue-43 decisions at wall-clock and deterministic node budgets."
    )
    parser.add_argument("--repeats", type=_positive, default=1)
    parser.add_argument("--time-budget-ms", type=_positive, action="append")
    parser.add_argument("--node-budget", type=_positive, action="append")
    parser.add_argument(
        "--position",
        action="append",
        metavar="CASE_ID",
        help="select a fixture case (repeat to select multiple; default: all)",
    )
    parser.add_argument("--json", action="store_true", help="emit JSON Lines")
    args = parser.parse_args(argv)

    time_budgets = _unique(
        args.time_budget_ms or DEFAULT_TIME_BUDGETS,
        "--time-budget-ms",
        parser,
    )
    node_budgets = _unique(
        args.node_budget or fixture_node_budgets,
        "--node-budget",
        parser,
    )
    positions_by_id = {case_id(position): position for position in positions}
    if args.position:
        unknown = [value for value in args.position if value not in positions_by_id]
        if unknown:
            parser.error(
                "unknown --position: "
                + ", ".join(unknown)
                + "; choose from "
                + ", ".join(positions_by_id)
            )
        if len(args.position) != len(set(args.position)):
            parser.error("--position must not contain duplicates")
        selected_positions = [positions_by_id[value] for value in args.position]
    else:
        selected_positions = positions

    rows = run_matrix(
        positions=selected_positions,
        time_budgets=time_budgets,
        node_budgets=node_budgets,
        repeats=args.repeats,
    )
    if args.json:
        for row in rows:
            print(json.dumps(row, sort_keys=True))
    else:
        _print_table(rows)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
