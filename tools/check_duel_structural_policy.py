#!/usr/bin/env python3
"""Audit Standard duel replays for structural root-policy violations."""

from __future__ import annotations

import argparse
from collections import Counter, deque
from dataclasses import dataclass
import json
import math
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
    post_search_override: bool
    justified_by_search: bool
    selected_move: str
    safe_alternatives: tuple[str, ...]
    unjustified_safe_alternatives: tuple[str, ...]
    justification_layers: tuple[str, ...]
    unknown_candidates: int


_OUTCOME_RANK = {"loss": 0, "unresolved": 1, "draw": 2, "win": 3}


def _outcome_interval(candidate: dict[str, Any]) -> tuple[int, int] | None:
    rank = _OUTCOME_RANK.get(str(candidate.get("minimax_outcome")))
    bound = candidate.get("minimax_bound")
    if rank is None or bound not in {"exact", "lower", "upper"}:
        return None
    return (0 if bound == "upper" else rank, 3 if bound == "lower" else rank)


def _numeric_interval(candidate: dict[str, Any]) -> tuple[float, float] | None:
    score = candidate.get("minimax_score")
    bound = candidate.get("minimax_bound")
    if not isinstance(score, (int, float)) or not math.isfinite(float(score)):
        return None
    if bound not in {"exact", "lower", "upper"}:
        return None
    value = float(score)
    return (
        -math.inf if bound == "upper" else value,
        math.inf if bound == "lower" else value,
    )


def _strict_search_dominance_layer(
    selected: dict[str, Any],
    alternative: dict[str, Any],
) -> str | None:
    """Independently prove a strict search layer; diagnostics reasons are ignored."""
    selected_outcome = _outcome_interval(selected)
    alternative_outcome = _outcome_interval(alternative)
    if selected_outcome is None or alternative_outcome is None:
        return None
    if selected_outcome[0] > alternative_outcome[1]:
        return "outcome_interval"
    if alternative_outcome[0] > selected_outcome[1]:
        return None

    # Production applies the independent SAFE-over-deficient-UNKNOWN relation
    # before numeric heuristic ordering on unresolved frontiers.
    if (
        selected.get("minimax_outcome") == "unresolved"
        and alternative.get("minimax_outcome") == "unresolved"
    ):
        return None

    both_exact_losses = (
        selected.get("minimax_outcome") == "loss"
        and selected.get("minimax_bound") == "exact"
        and isinstance(selected.get("minimax_terminal_distance"), int)
        and alternative.get("minimax_outcome") == "loss"
        and alternative.get("minimax_bound") == "exact"
        and isinstance(alternative.get("minimax_terminal_distance"), int)
    )
    if both_exact_losses:
        selected_distance = int(selected["minimax_terminal_distance"])
        alternative_distance = int(alternative["minimax_terminal_distance"])
        if selected_distance > alternative_distance:
            return "terminal_survival"
        if selected_distance < alternative_distance:
            return None

    selected_numeric = _numeric_interval(selected)
    alternative_numeric = _numeric_interval(alternative)
    if (
        selected_numeric is not None
        and alternative_numeric is not None
        and selected_numeric[0] > alternative_numeric[1]
    ):
        return "numeric_interval"
    return None


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
    reply_outcomes = selected.get("reply_outcomes", {})
    complete_opponent_replies = frozenset(
        str(move) for move in diagnostics.get("complete_opponent_replies", ())
    )
    guaranteed_terminal_non_loss = (
        bool(complete_opponent_replies)
        and frozenset(reply_outcomes) == complete_opponent_replies
        and all(outcome in {"win", "draw"} for outcome in reply_outcomes.values())
    )
    capacity_deficient = int(selected["relaxed_static_capacity"]) < int(
        selected["post_move_length"]
    )
    structural_conflict = (
        capacity_deficient
        and selected["structural_proof"] != "safe"
        and bool(safe_alternatives)
        and not guaranteed_terminal_non_loss
    )
    layers: list[str] = []
    unjustified: list[str] = []
    if structural_conflict:
        for move in safe_alternatives:
            layer = _strict_search_dominance_layer(
                selected, candidates[move]
            )
            if layer is None:
                unjustified.append(move)
            else:
                layers.append(layer)
    post_search_override = structural_conflict and diagnostics.get("selection_reason") == "corridor_guard"
    justified_by_search = structural_conflict and not unjustified and bool(layers)
    violation = structural_conflict and not post_search_override and not justified_by_search
    return DiagnosticsAudit(
        violation,
        post_search_override,
        justified_by_search,
        selected_move,
        safe_alternatives,
        tuple(unjustified),
        tuple(sorted(set(layers))),
        unknown_candidates,
    )


def _complete_opponent_replies(board: Board, snake_id: str) -> tuple[str, ...]:
    """Return the complete reply set only when the native profile proves it."""
    profile = duel_root_profile(board, snake_id)
    reply_sets = {
        frozenset(str(reply) for reply in candidate["reply_outcomes"])
        for candidate in profile.values()
        if candidate["evaluated"]
    }
    if len(reply_sets) != 1:
        return ()
    replies = next(iter(reply_sets))
    return tuple(sorted(replies)) if replies else ()


def should_run_diagnostics(board: Board, snake_id: str) -> bool:
    """Return whether a conservative static prefilter cannot rule out deficiency.

    A violation needs one proved-safe alternative with a live reply, so a board
    with no live-reply command can be skipped.  The selected move itself need
    not have a live reply.  The diagnostics predicate deliberately makes no
    further assumption about which command production selected.  We therefore
    check the capacity lower bound for every evaluated command, regardless of
    board-rule safety or live-reply count.

    The flood fill permanently blocks every currently occupied cell (apart
    from the destination).  This is a lower bound on
    ``relaxed_static_capacity`` because the production relaxed board removes
    the opponent and permits our tail to vacate.  If every eligible lower bound
    already fits the post-move length, no production-selected command can be
    capacity-deficient.  Any uncertainty fails open to full diagnostics.
    """
    profile = duel_root_profile(board, snake_id)
    live_moves = [
        move
        for move, candidate in profile.items()
        if candidate["evaluated"] and int(candidate["alive_reply_count"]) > 0
    ]
    if not live_moves:
        return False

    selected_candidates = [
        move
        for move, candidate in profile.items()
        if candidate["evaluated"]
    ]

    snake = board.snakes[snake_id]
    occupied = {
        (coord.x, coord.y)
        for other in board.snakes.values()
        for coord in other.body
    }
    food = {(coord.x, coord.y) for coord in board.food}
    head = board.head(snake_id)
    own_body = {(coord.x, coord.y) for coord in snake.body}
    for move in selected_candidates:
        destination = board.step(head, move)
        start = (destination.x, destination.y)
        if start in own_body:
            # Whether a tail cell vacates depends on eating and duplicate-tail
            # state.  Zero is always a truthful lower bound and fails open.
            capacity = 0
        else:
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
    frame: dict[str, Any], following: dict[str, Any] | None, snake_id: str
) -> str | None:
    if following is None:
        return None
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


def _summary(export_root: Path, budget_ms: int, prefilter: bool) -> dict[str, Any]:
    return {
        "schema_version": 1,
        "export_root": str(export_root),
        "budget_ms": budget_ms,
        "prefilter": prefilter,
        "files_discovered": 0,
        "replay_root_frames_seen": 0,
        "standard_duel_root_frames": 0,
        "prefiltered_root_frames": 0,
        "diagnostics_root_frames": 0,
        "unknown_candidate_proofs": 0,
        "justified_search_selections": 0,
        "comparator_violations": [],
        "post_search_overrides": [],
        "cutoff_candidate_counts": {},
        "violations": [],
        "error_count": 0,
        "errors": [],
    }


def _replay_structure_error(export: object) -> str | None:
    if not isinstance(export, dict):
        return "top-level JSON value is not an object"
    game = export.get("game")
    frames = export.get("frames")
    if not isinstance(game, dict):
        return "game is not an object"
    if "Width" not in game or "Height" not in game:
        return "game is missing Width or Height"
    try:
        width = int(game["Width"])
        height = int(game["Height"])
    except (TypeError, ValueError):
        return "game Width or Height is not an integer"
    if width <= 0 or height <= 0:
        return "game Width and Height must be positive"
    ruleset_error = _ruleset_structure_error(game)
    if ruleset_error is not None:
        return ruleset_error
    if not isinstance(frames, list):
        return "frames is not an array"
    for index, frame in enumerate(frames):
        if not isinstance(frame, dict):
            return f"frame {index} is not an object"
        if not isinstance(frame.get("Snakes"), list):
            return f"frame {index} Snakes is not an array"
        try:
            int(frame["Turn"])
        except (KeyError, TypeError, ValueError):
            return f"frame {index} Turn is missing or not an integer"
        for collection_name in ("Food", "Hazards"):
            collection = frame.get(collection_name, [])
            if not isinstance(collection, list):
                return f"frame {index} {collection_name} is not an array"
            for coord_index, coord in enumerate(collection):
                if not _valid_export_coord(coord):
                    return (
                        f"frame {index} {collection_name}[{coord_index}] "
                        "is not an X/Y coordinate"
                    )
        for snake_index, snake in enumerate(frame["Snakes"]):
            if not isinstance(snake, dict):
                return f"frame {index} Snakes[{snake_index}] is not an object"
            if "ID" not in snake:
                return f"frame {index} Snakes[{snake_index}] is missing ID"
            body = snake.get("Body")
            if not isinstance(body, list):
                return f"frame {index} Snakes[{snake_index}] Body is not an array"
            for coord_index, coord in enumerate(body):
                if not _valid_export_coord(coord):
                    return (
                        f"frame {index} Snakes[{snake_index}] Body[{coord_index}] "
                        "is not an X/Y coordinate"
                    )
            try:
                int(snake.get("Health", 100))
            except (TypeError, ValueError):
                return f"frame {index} Snakes[{snake_index}] Health is not an integer"
    return None


def _ruleset_structure_error(game: dict[str, Any]) -> str | None:
    if "RulesetName" in game and not isinstance(game["RulesetName"], str):
        return "game RulesetName is not a string"
    ruleset = game.get("Ruleset")
    if ruleset is None:
        return None
    if not isinstance(ruleset, dict):
        return "game Ruleset is not an object"
    if "name" in ruleset and not isinstance(ruleset["name"], str):
        return "game Ruleset.name is not a string"
    settings = ruleset.get("settings")
    if settings is not None and not isinstance(settings, dict):
        return "game Ruleset.settings is not an object"
    hazard_values = []
    if "hazardDamagePerTurn" in ruleset:
        hazard_values.append(ruleset["hazardDamagePerTurn"])
    if isinstance(settings, dict) and "hazardDamagePerTurn" in settings:
        hazard_values.append(settings["hazardDamagePerTurn"])
    try:
        for value in hazard_values:
            int(value)
    except (TypeError, ValueError, OverflowError):
        return "game hazardDamagePerTurn is not an integer"
    return None


def _valid_export_coord(coord: object) -> bool:
    if not isinstance(coord, dict) or "X" not in coord or "Y" not in coord:
        return False
    try:
        int(coord["X"])
        int(coord["Y"])
    except (TypeError, ValueError):
        return False
    return True


def _record_error(
    summary: dict[str, Any], *, kind: str, path: Path, detail: str
) -> None:
    summary["errors"].append(
        {"kind": kind, "path": str(path), "detail": detail}
    )
    summary["error_count"] = len(summary["errors"])


def _record_root_error(
    summary: dict[str, Any],
    *,
    path: Path,
    game_id: str,
    turn: int,
    stage: str,
    error: Exception,
) -> None:
    summary["errors"].append(
        {
            "kind": "root_processing_error",
            "path": str(path),
            "game_id": game_id,
            "turn": turn,
            "stage": stage,
            "detail": str(error),
        }
    )
    summary["error_count"] = len(summary["errors"])


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
    summary = _summary(args.export_root, args.budget_ms, not args.no_prefilter)
    if not args.export_root.exists():
        _record_error(
            summary,
            kind="missing_export_root",
            path=args.export_root,
            detail="export root does not exist",
        )
        _print_summary(summary, as_json=args.json)
        return 2
    try:
        paths = export_paths(args.export_root)
    except OSError as error:
        _record_error(
            summary,
            kind="unreadable_export_root",
            path=args.export_root,
            detail=str(error),
        )
        _print_summary(summary, as_json=args.json)
        return 2
    summary["files_discovered"] = len(paths)
    cutoff_counts: Counter[str] = Counter()
    stop = False
    for path in paths:
        try:
            export = json.loads(path.read_text(encoding="utf-8"))
        except OSError as error:
            _record_error(
                summary,
                kind="unreadable_export",
                path=path,
                detail=str(error),
            )
            continue
        except UnicodeDecodeError as error:
            _record_error(
                summary,
                kind="invalid_encoding",
                path=path,
                detail=str(error),
            )
            continue
        except json.JSONDecodeError as error:
            _record_error(
                summary,
                kind="invalid_json",
                path=path,
                detail=f"line {error.lineno} column {error.colno}: {error.msg}",
            )
            continue
        structure_error = _replay_structure_error(export)
        if structure_error is not None:
            _record_error(
                summary,
                kind="malformed_replay",
                path=path,
                detail=structure_error,
            )
            continue
        game = export.get("game")
        frames = export.get("frames")
        assert isinstance(game, dict)
        assert isinstance(frames, list)
        game_id = _game_id(export, path)
        if args.game_ids and game_id not in args.game_ids:
            continue
        if _ruleset_name(game) != "standard":
            continue
        for index, frame in enumerate(frames):
            assert isinstance(frame, dict)
            following = frames[index + 1] if index + 1 < len(frames) else None
            assert following is None or isinstance(following, dict)
            summary["replay_root_frames_seen"] += 1
            turn = int(frame.get("Turn", -1))
            if args.turns and turn not in args.turns:
                continue
            try:
                alive = _alive_snakes(frame)
                if len(alive) != 2:
                    continue
                ours = [
                    snake for snake in alive if snake.get("Name") == args.snake_name
                ]
                if len(ours) != 1:
                    continue
                snake_id = str(ours[0]["ID"])
                actual_move = _actual_move(frame, following, snake_id)
                alive_frame = dict(frame)
                alive_frame["Snakes"] = alive
                board = _board_from_frame(game, alive_frame)
            except Exception as error:
                _record_root_error(
                    summary,
                    path=path,
                    game_id=game_id,
                    turn=turn,
                    stage="board_conversion",
                    error=error,
                )
                break
            summary["standard_duel_root_frames"] += 1
            limit_reached = (
                args.limit is not None
                and summary["standard_duel_root_frames"] == args.limit
            )
            if not args.no_prefilter:
                try:
                    run_full_diagnostics = should_run_diagnostics(board, snake_id)
                except Exception as error:
                    _record_root_error(
                        summary,
                        path=path,
                        game_id=game_id,
                        turn=turn,
                        stage="prefilter",
                        error=error,
                    )
                    stop = limit_reached
                    break
                if not run_full_diagnostics:
                    summary["prefiltered_root_frames"] += 1
                    if limit_reached:
                        stop = True
                        break
                    continue
            try:
                diagnostics = run_diagnostics(
                    board, snake_id, time_budget_ms=args.budget_ms
                )
                diagnostics = dict(diagnostics)
                diagnostics["complete_opponent_replies"] = _complete_opponent_replies(
                    board, snake_id
                )
                summary["diagnostics_root_frames"] += 1
                audit = audit_diagnostics(diagnostics)
                summary["unknown_candidate_proofs"] += audit.unknown_candidates
                for candidate in diagnostics["root_candidates"].values():
                    if candidate["structural_proof"] == "unknown":
                        cutoff_counts[str(candidate.get("proof_cutoff", "none"))] += 1
                if audit.violation:
                    record = {
                        "game_id": game_id,
                        "turn": turn,
                        "actual_move": actual_move,
                        "selected_move": audit.selected_move,
                        "safe_alternatives": list(audit.safe_alternatives),
                        "unjustified_safe_alternatives": list(
                            audit.unjustified_safe_alternatives
                        ),
                    }
                    summary["comparator_violations"].append(record)
                    summary["violations"].append(record)
                elif audit.post_search_override:
                    summary["post_search_overrides"].append(
                        {
                            "game_id": game_id,
                            "turn": turn,
                            "actual_move": actual_move,
                            "selected_move": audit.selected_move,
                            "safe_alternatives": list(audit.safe_alternatives),
                            "kind": "post_search_override",
                        }
                    )
                elif audit.justified_by_search:
                    summary["justified_search_selections"] += 1
            except Exception as error:
                _record_root_error(
                    summary,
                    path=path,
                    game_id=game_id,
                    turn=turn,
                    stage="diagnostics",
                    error=error,
                )
                stop = limit_reached
                break
            if limit_reached:
                stop = True
                break
        if stop:
            break
    summary["cutoff_candidate_counts"] = dict(sorted(cutoff_counts.items()))
    _print_summary(summary, as_json=args.json)
    if summary["error_count"]:
        return 2
    return 1 if summary["comparator_violations"] else 0


def _print_summary(summary: dict[str, Any], *, as_json: bool) -> None:
    if as_json:
        print(json.dumps(summary, sort_keys=True))
    else:
        cutoff_summary = ",".join(
            f"{cutoff}:{count}"
            for cutoff, count in summary["cutoff_candidate_counts"].items()
        ) or "none"
        print(
            "duel structural policy audit: "
            f"files_discovered={summary['files_discovered']} "
            f"standard_duel_root_frames={summary['standard_duel_root_frames']} "
            f"prefiltered_root_frames={summary['prefiltered_root_frames']} "
            f"diagnostics_root_frames={summary['diagnostics_root_frames']} "
            f"unknown_candidate_proofs={summary['unknown_candidate_proofs']} "
            f"cutoff_candidate_counts={cutoff_summary} "
            f"justified_search_selections={summary['justified_search_selections']} "
            f"comparator_violations={len(summary['comparator_violations'])} "
            f"post_search_overrides={len(summary['post_search_overrides'])} "
            f"errors={summary['error_count']} "
            f"budget_ms={summary['budget_ms']}"
        )
        for violation in summary["comparator_violations"]:
            print(
                f"VIOLATION {violation['game_id']} T{violation['turn']}: "
                f"actual={violation['actual_move']} selected={violation['selected_move']} "
                f"safe_alternatives={','.join(violation['safe_alternatives'])}"
            )
        for override in summary["post_search_overrides"]:
            print(
                f"POST_SEARCH_OVERRIDE {override['game_id']} T{override['turn']}: "
                f"actual={override['actual_move']} selected={override['selected_move']} "
                f"safe_alternatives={','.join(override['safe_alternatives'])}"
            )
        for error in summary["errors"]:
            root_context = ""
            if error["kind"] == "root_processing_error":
                root_context = (
                    f" game_id={error['game_id']} turn={error['turn']}"
                    f" stage={error['stage']}"
                )
            print(
                f"ERROR {error['kind']} {error['path']}{root_context}: {error['detail']}"
            )


if __name__ == "__main__":
    raise SystemExit(main())
