#!/usr/bin/env python3
"""Audit Standard duel replays for structural root-policy violations."""

from __future__ import annotations

import argparse
from collections import Counter, deque
from dataclasses import dataclass
import json
from pathlib import Path
import sys
from typing import Any, Callable, Sequence

REPO_ROOT = Path(__file__).resolve().parents[1]
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from battlesnake.battlesnake_native import Board, duel_root_profile, minimax_diagnostics
from tools.tuning.replay_dataset import (
    _board_from_frame,
    _point,
    _ruleset_name,
    export_paths,
    infer_move,
)


@dataclass(frozen=True)
class DiagnosticsAudit:
    violation: bool
    selected_move: str
    safe_alternatives: tuple[str, ...]
    unknown_candidates: int


def audit_diagnostics(diagnostics: dict[str, Any]) -> DiagnosticsAudit:
    """Classify one production diagnostics record."""
    selected_move = str(diagnostics["move"])
    candidates = diagnostics["root_candidates"]
    selected = candidates[selected_move]
    safe_alternatives = tuple(
        sorted(
            str(move)
            for move, candidate in candidates.items()
            if move != selected_move
            and candidate["structural_proof"] == "safe"
            and int(candidate["alive_reply_count"]) > 0
        )
    )
    unknown_candidates = sum(
        candidate["structural_proof"] == "unknown"
        for candidate in candidates.values()
    )
    reply_outcomes = tuple(selected.get("reply_outcomes", {}).values())
    guaranteed_draw = (
        selected.get("minimax_outcome") == "draw"
        and int(selected["alive_reply_count"]) == 0
        and bool(reply_outcomes)
        and all(outcome == "draw" for outcome in reply_outcomes)
    )
    capacity_deficient = int(selected["relaxed_static_capacity"]) < int(
        selected["post_move_length"]
    )
    violation = (
        capacity_deficient
        and selected["structural_proof"] != "safe"
        and bool(safe_alternatives)
        and not guaranteed_draw
    )
    return DiagnosticsAudit(
        violation,
        selected_move,
        safe_alternatives,
        unknown_candidates,
    )


def should_run_diagnostics(board: Board, snake_id: str) -> bool:
    """Return whether a conservative static prefilter cannot rule out deficiency.

    A violating selected move and its proved-safe alternative both have a live
    reply under the production root ladder, so fewer than two live-reply root
    commands cannot violate the invariant.  For the remaining commands, the
    flood fill permanently blocks every currently occupied cell (apart from the
    destination).  This is a lower bound on ``relaxed_static_capacity`` because
    the production relaxed board removes the opponent and permits our tail to
    vacate.  If every lower bound already fits the post-move length, no selected
    command can be capacity-deficient.  Any uncertainty fails open to the full
    production diagnostics call.
    """
    profile = duel_root_profile(board, snake_id)
    live_moves = [
        move
        for move, candidate in profile.items()
        if candidate["evaluated"] and int(candidate["alive_reply_count"]) > 0
    ]
    if len(live_moves) < 2:
        return False

    snake = board.snakes[snake_id]
    occupied = {
        (coord.x, coord.y)
        for other in board.snakes.values()
        for coord in other.body
    }
    food = {(coord.x, coord.y) for coord in board.food}
    head = board.head(snake_id)
    for move in live_moves:
        destination = board.step(head, move)
        start = (destination.x, destination.y)
        blocked = occupied - {start}
        capacity = _flood_capacity(board.width, board.height, start, blocked)
        post_move_length = int(snake.length) + (start in food)
        if capacity < post_move_length:
            return True
    return False


def _flood_capacity(
    width: int,
    height: int,
    start: tuple[int, int],
    blocked: set[tuple[int, int]],
) -> int:
    if not (0 <= start[0] < width and 0 <= start[1] < height) or start in blocked:
        return 0
    visited = {start}
    queue = deque([start])
    while queue:
        x, y = queue.popleft()
        for adjacent in ((x, y + 1), (x, y - 1), (x - 1, y), (x + 1, y)):
            if (
                0 <= adjacent[0] < width
                and 0 <= adjacent[1] < height
                and adjacent not in blocked
                and adjacent not in visited
            ):
                visited.add(adjacent)
                queue.append(adjacent)
    return len(visited)


def _alive_snakes(frame: dict[str, Any]) -> list[dict[str, Any]]:
    return [
        snake
        for snake in frame.get("Snakes", [])
        if snake.get("Death") is None and snake.get("Body")
    ]


def _game_id(export: dict[str, Any], path: Path) -> str:
    game = export.get("game", {})
    return str(export.get("game_id") or game.get("ID") or path.stem)


def _actual_move(
    frame: dict[str, Any], following: dict[str, Any], snake_id: str
) -> str | None:
    if int(following.get("Turn", -1)) != int(frame["Turn"]) + 1:
        return None
    current = next(
        (snake for snake in _alive_snakes(frame) if str(snake.get("ID")) == snake_id),
        None,
    )
    next_snake = next(
        (snake for snake in _alive_snakes(following) if str(snake.get("ID")) == snake_id),
        None,
    )
    if current is None or next_snake is None:
        return None
    return infer_move(_point(current["Body"][0]), _point(next_snake["Body"][0]))


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--export-root", type=Path, required=True)
    parser.add_argument("--budget-ms", type=int, default=100)
    parser.add_argument("--snake-name", default="scvnak")
    parser.add_argument("--game-id", action="append", dest="game_ids")
    parser.add_argument("--turn", action="append", type=int, dest="turns")
    parser.add_argument("--limit", type=int)
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--no-prefilter", action="store_true")
    return parser


def main(
    argv: Sequence[str] | None = None,
    *,
    diagnostics_fn: Callable[..., dict[str, Any]] | None = None,
) -> int:
    """Run the command-line checker."""
    args = _parser().parse_args(argv)
    if args.budget_ms <= 0:
        _parser().error("--budget-ms must be positive")
    if args.limit is not None and args.limit <= 0:
        _parser().error("--limit must be positive")
    run_diagnostics = diagnostics_fn or minimax_diagnostics
    paths = export_paths(args.export_root)
    summary: dict[str, Any] = {
        "schema_version": 1,
        "export_root": str(args.export_root),
        "budget_ms": args.budget_ms,
        "prefilter": not args.no_prefilter,
        "files_seen": len(paths),
        "frames_seen": 0,
        "standard_duel_frames": 0,
        "prefiltered_frames": 0,
        "scanned_frames": 0,
        "unknown_candidates": 0,
        "cutoff_counts": {},
        "violations": [],
    }
    cutoff_counts: Counter[str] = Counter()
    stop = False
    for path in paths:
        try:
            export = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            continue
        if not isinstance(export, dict):
            continue
        game = export.get("game")
        frames = export.get("frames")
        if not isinstance(game, dict) or not isinstance(frames, list):
            continue
        game_id = _game_id(export, path)
        if args.game_ids and game_id not in args.game_ids:
            continue
        if _ruleset_name(game) != "standard":
            continue
        for index in range(len(frames) - 1):
            frame = frames[index]
            following = frames[index + 1]
            if not isinstance(frame, dict) or not isinstance(following, dict):
                continue
            summary["frames_seen"] += 1
            turn = int(frame.get("Turn", -1))
            if args.turns and turn not in args.turns:
                continue
            alive = _alive_snakes(frame)
            if len(alive) != 2:
                continue
            ours = [snake for snake in alive if snake.get("Name") == args.snake_name]
            if len(ours) != 1:
                continue
            snake_id = str(ours[0]["ID"])
            actual_move = _actual_move(frame, following, snake_id)
            if actual_move is None:
                continue
            summary["standard_duel_frames"] += 1
            alive_frame = dict(frame)
            alive_frame["Snakes"] = alive
            board = _board_from_frame(game, alive_frame)
            if not args.no_prefilter and not should_run_diagnostics(board, snake_id):
                summary["prefiltered_frames"] += 1
            else:
                diagnostics = run_diagnostics(
                    board, snake_id, time_budget_ms=args.budget_ms
                )
                summary["scanned_frames"] += 1
                audit = audit_diagnostics(diagnostics)
                summary["unknown_candidates"] += audit.unknown_candidates
                for candidate in diagnostics["root_candidates"].values():
                    if candidate["structural_proof"] == "unknown":
                        cutoff_counts[str(candidate.get("proof_cutoff", "none"))] += 1
                if audit.violation:
                    summary["violations"].append(
                        {
                            "game_id": game_id,
                            "turn": turn,
                            "actual_move": actual_move,
                            "selected_move": audit.selected_move,
                            "safe_alternatives": list(audit.safe_alternatives),
                        }
                    )
            if args.limit is not None and summary["standard_duel_frames"] >= args.limit:
                stop = True
                break
        if stop:
            break
    summary["cutoff_counts"] = dict(sorted(cutoff_counts.items()))
    if args.json:
        print(json.dumps(summary, sort_keys=True))
    else:
        cutoff_summary = ",".join(
            f"{cutoff}:{count}" for cutoff, count in summary["cutoff_counts"].items()
        ) or "none"
        print(
            "duel structural policy audit: "
            f"files={summary['files_seen']} "
            f"standard_duel_frames={summary['standard_duel_frames']} "
            f"prefiltered={summary['prefiltered_frames']} "
            f"scanned={summary['scanned_frames']} "
            f"unknown_candidates={summary['unknown_candidates']} "
            f"cutoffs={cutoff_summary} "
            f"violations={len(summary['violations'])} "
            f"budget_ms={summary['budget_ms']}"
        )
        for violation in summary["violations"]:
            print(
                f"VIOLATION {violation['game_id']} T{violation['turn']}: "
                f"actual={violation['actual_move']} selected={violation['selected_move']} "
                f"safe_alternatives={','.join(violation['safe_alternatives'])}"
            )
    return 1 if summary["violations"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
