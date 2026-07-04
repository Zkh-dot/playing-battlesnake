from __future__ import annotations

import argparse
import json
import random
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Literal

from battlesnake.battlesnake_native import Board, Coord, Snake, minimax_diagnostics
from benchmarks.scenarios import Scenario
from tools.tuning.evaluate_weights import load_weights


Agent = Literal["before", "after"]
Winner = Literal["before", "after", "draw"]


@dataclass(frozen=True)
class MatchResult:
    match: int
    scenario: str
    after_side: int
    winner: Winner
    turns: int
    before_alive: bool
    after_alive: bool
    before_length: int
    after_length: int
    before_health: int
    after_health: int
    before_errors: int
    after_errors: int


def _clone_snake(source: Snake, snake_id: Agent) -> Snake:
    return Snake(
        id=snake_id,
        name=snake_id,
        health=source.health,
        body=tuple(source.body),
        head=source.head,
        length=source.length,
    )


def _standard_duel_scenarios() -> list[Scenario]:
    return [_generated_standard_duel_scenario(seed) for seed in range(20)]


def _generated_standard_duel_scenario(seed: int) -> Scenario:
    rng = random.Random(seed)
    width = 11
    height = 11
    left_length = rng.randint(3, 5)
    right_length = rng.randint(3, 5)
    left_y = rng.randint(2, height - 3)
    right_y = rng.randint(2, height - 3)
    left_head_x = rng.randint(left_length - 1, 4)
    right_head_x = width - 1 - rng.randint(right_length - 1, 4)
    left_body = tuple(Coord(left_head_x - offset, left_y) for offset in range(left_length))
    right_body = tuple(Coord(right_head_x + offset, right_y) for offset in range(right_length))
    occupied = {(coord.x, coord.y) for coord in left_body + right_body}
    food: list[Coord] = []
    while len(food) < 5:
        coord = (rng.randint(1, width - 2), rng.randint(1, height - 2))
        if coord in occupied or coord in {(item.x, item.y) for item in food}:
            continue
        food.append(Coord(*coord))
    return Scenario(
        name=f"generated_standard_duel_{seed}",
        width=width,
        height=height,
        ruleset_name="standard",
        hazard_damage=0,
        snakes=(
            Snake("left", "left", 90, left_body, left_body[0], left_length),
            Snake("right", "right", 90, right_body, right_body[0], right_length),
        ),
        food=tuple(food),
        hazards=(),
    )


def _build_match_board(scenario: Scenario, after_side: int) -> Board:
    before_side = 1 - after_side
    snakes = (
        _clone_snake(scenario.snakes[before_side], "before"),
        _clone_snake(scenario.snakes[after_side], "after"),
    )
    return Board(
        scenario.width,
        scenario.height,
        snakes,
        scenario.food,
        scenario.hazards,
        scenario.ruleset_name,
        scenario.hazard_damage,
    )


def _snake_status(board: Board, snake_id: Agent) -> tuple[bool, int, int]:
    snake = board.snakes.get(snake_id)
    if snake is None:
        return False, 0, 0
    return True, snake.length, snake.health


def _choose_move(
    board: Board,
    snake_id: Agent,
    weights: dict[str, float],
    *,
    fixed_depth: int,
    time_budget_ms: int,
) -> tuple[str, bool]:
    try:
        diagnostics = minimax_diagnostics(
            board,
            snake_id,
            time_budget_ms=time_budget_ms,
            fixed_depth=fixed_depth,
            enable_tt=True,
            enable_move_ordering=True,
            enable_make_unmake=True,
            weights=weights,
        )
        return str(diagnostics["move"]), False
    except Exception:
        safe_moves = board.safe_moves(snake_id)
        if safe_moves:
            return safe_moves[0], True
        return "up", True


def _winner(board: Board) -> Winner | None:
    before_alive = "before" in board.snakes
    after_alive = "after" in board.snakes
    if before_alive and after_alive:
        return None
    if not before_alive and not after_alive:
        return "draw"
    if before_alive:
        return "before"
    return "after"


def play_match(
    *,
    match_index: int,
    scenario: Scenario,
    after_side: int,
    before_weights: dict[str, float],
    after_weights: dict[str, float],
    fixed_depth: int,
    time_budget_ms: int,
    max_turns: int,
) -> MatchResult:
    board = _build_match_board(scenario, after_side)
    before_errors = 0
    after_errors = 0
    turns = 0
    result: Winner | None = None

    for turn in range(max_turns):
        result = _winner(board)
        if result is not None:
            turns = turn
            break

        before_move, before_error = _choose_move(
            board,
            "before",
            before_weights,
            fixed_depth=fixed_depth,
            time_budget_ms=time_budget_ms,
        )
        after_move, after_error = _choose_move(
            board,
            "after",
            after_weights,
            fixed_depth=fixed_depth,
            time_budget_ms=time_budget_ms,
        )
        before_errors += int(before_error)
        after_errors += int(after_error)
        board = board.clone_and_apply({"before": before_move, "after": after_move})
        turns = turn + 1
        result = _winner(board)
        if result is not None:
            break
    else:
        result = "draw"

    before_alive, before_length, before_health = _snake_status(board, "before")
    after_alive, after_length, after_health = _snake_status(board, "after")

    return MatchResult(
        match=match_index,
        scenario=scenario.name,
        after_side=after_side,
        winner=result,
        turns=turns,
        before_alive=before_alive,
        after_alive=after_alive,
        before_length=before_length,
        after_length=after_length,
        before_health=before_health,
        after_health=after_health,
        before_errors=before_errors,
        after_errors=after_errors,
    )


def summarize(results: list[MatchResult]) -> dict[str, object]:
    return {
        "matches": len(results),
        "before_wins": sum(result.winner == "before" for result in results),
        "after_wins": sum(result.winner == "after" for result in results),
        "draws": sum(result.winner == "draw" for result in results),
        "before_errors": sum(result.before_errors for result in results),
        "after_errors": sum(result.after_errors for result in results),
        "average_turns": sum(result.turns for result in results) / len(results) if results else 0.0,
    }


def write_markdown(path: Path, summary: dict[str, object], results: list[MatchResult]) -> None:
    lines = [
        "# Weight Match Comparison",
        "",
        "Summary:",
        "",
        f"- matches: {summary['matches']}",
        f"- before wins: {summary['before_wins']}",
        f"- after wins: {summary['after_wins']}",
        f"- draws: {summary['draws']}",
        f"- before errors: {summary['before_errors']}",
        f"- after errors: {summary['after_errors']}",
        f"- average turns: {summary['average_turns']:.2f}",
        "",
        "| match | scenario | after side | winner | turns | before length | after length |",
        "| ---: | --- | ---: | --- | ---: | ---: | ---: |",
    ]
    for result in results:
        lines.append(
            f"| {result.match} | {result.scenario} | {result.after_side} | "
            f"{result.winner} | {result.turns} | {result.before_length} | {result.after_length} |"
        )
    path.write_text("\n".join(lines) + "\n")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--before-weights", type=Path, default=Path("configs/evaluation_weights/default.json"))
    parser.add_argument("--after-weights", type=Path, required=True)
    parser.add_argument("--matches", type=int, default=20)
    parser.add_argument("--fixed-depth", type=int, default=3)
    parser.add_argument("--time-budget-ms", type=int, default=5000)
    parser.add_argument("--max-turns", type=int, default=200)
    parser.add_argument("--output", type=Path, default=Path("artifacts/weight_tuning/match_comparison.json"))
    parser.add_argument("--markdown-output", type=Path, default=Path("artifacts/weight_tuning/match_comparison.md"))
    args = parser.parse_args()

    scenarios = _standard_duel_scenarios()
    if not scenarios:
        raise RuntimeError("no standard duel scenarios available")

    before_weights = load_weights(args.before_weights)
    after_weights = load_weights(args.after_weights)
    results = [
        play_match(
            match_index=index,
            scenario=scenarios[index % len(scenarios)],
            after_side=index % 2,
            before_weights=before_weights,
            after_weights=after_weights,
            fixed_depth=args.fixed_depth,
            time_budget_ms=args.time_budget_ms,
            max_turns=args.max_turns,
        )
        for index in range(args.matches)
    ]
    summary = summarize(results)
    payload = {
        "summary": summary,
        "settings": {
            "fixed_depth": args.fixed_depth,
            "time_budget_ms": args.time_budget_ms,
            "max_turns": args.max_turns,
        },
        "results": [asdict(result) for result in results],
    }

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")
    write_markdown(args.markdown_output, summary, results)
    print(json.dumps(summary, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
