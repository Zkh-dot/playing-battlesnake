#!/usr/bin/env python3
"""Evaluate exported Battlesnake replays with the C position evaluator."""

from __future__ import annotations

import argparse
import csv
import json
import os
import random
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Any


MOVES = ("up", "down", "left", "right")
MOVE_BY_DELTA = {
    (0, 1): "up",
    (0, -1): "down",
    (-1, 0): "left",
    (1, 0): "right",
}


@dataclass(frozen=True)
class CaseMeta:
    case_id: str
    game_id: str
    turn: int
    kind: str
    first_id: str
    second_id: str
    snake_id: str
    actual_move: str
    candidate_move: str
    winner_id: str | None
    snake0_id: str
    snake1_id: str


def coord(value: dict[str, Any]) -> tuple[int, int]:
    return int(value["X"]), int(value["Y"])


def snake_alive(snake: dict[str, Any] | None) -> bool:
    return bool(snake and snake.get("Body") and not snake.get("Death"))


def snakes_by_id(frame: dict[str, Any]) -> dict[str, dict[str, Any]]:
    return {snake["ID"]: snake for snake in frame.get("Snakes", [])}


def head_move(current: dict[str, Any], next_snake: dict[str, Any]) -> str | None:
    old_head = coord(current["Body"][0])
    new_head = coord(next_snake["Body"][0])
    return MOVE_BY_DELTA.get((new_head[0] - old_head[0], new_head[1] - old_head[1]))


def winner_id(frames: list[dict[str, Any]], snake_ids: tuple[str, str]) -> str | None:
    final = snakes_by_id(frames[-1])
    alive = [snake_id for snake_id in snake_ids if snake_alive(final.get(snake_id))]
    if len(alive) == 1:
        return alive[0]
    deaths = {
        snake_id: final.get(snake_id, {}).get("Death", {}).get("Turn")
        for snake_id in snake_ids
        if final.get(snake_id, {}).get("Death")
    }
    if len(deaths) == 2 and deaths[snake_ids[0]] != deaths[snake_ids[1]]:
        return max(deaths, key=lambda snake_id: deaths[snake_id])
    return None


def sanitize_token(value: str) -> str:
    return value.replace("\t", "_").replace("\n", "_").replace(" ", "_")


def emit_board(lines: list[str], game: dict[str, Any], frame: dict[str, Any], snake_ids: tuple[str, str]) -> None:
    game_info = game.get("game", {})
    width = int(game_info.get("Width", 11))
    height = int(game_info.get("Height", 11))
    hazard_damage = int(game_info.get("Ruleset", {}).get("hazardDamagePerTurn", 0) or 0)
    ruleset = "duel"

    frame_snakes = snakes_by_id(frame)
    snakes = [frame_snakes[snake_id] for snake_id in snake_ids if snake_alive(frame_snakes.get(snake_id))]
    lines.append(f"{width} {height} {hazard_damage}")
    lines.append(ruleset)
    lines.append(str(len(snakes)))
    for snake in snakes:
        body = [coord(part) for part in snake["Body"]]
        tokens = [sanitize_token(snake["ID"]), str(int(snake.get("Health", 0))), str(len(body))]
        for x, y in body:
            tokens.extend([str(x), str(y)])
        lines.append(" ".join(tokens))

    food = [coord(item) for item in frame.get("Food", [])]
    food_tokens = [str(len(food))]
    for x, y in food:
        food_tokens.extend([str(x), str(y)])
    lines.append(" ".join(food_tokens))

    hazards = [coord(item) for item in frame.get("Hazards", [])]
    hazard_tokens = [str(len(hazards))]
    for x, y in hazards:
        hazard_tokens.extend([str(x), str(y)])
    lines.append(" ".join(hazard_tokens))


def add_case(
    lines: list[str],
    metas: list[CaseMeta],
    game: dict[str, Any],
    frame: dict[str, Any],
    *,
    case_id: str,
    first_id: str,
    second_id: str,
    budget_ms: int,
    max_depth: int,
    apply_first: str,
    apply_second: str,
    meta: CaseMeta,
) -> None:
    lines.append(case_id)
    lines.append(sanitize_token(first_id))
    lines.append(sanitize_token(second_id))
    lines.append(f"{budget_ms} {max_depth} 0")
    lines.append(apply_first)
    lines.append(apply_second)
    emit_board(lines, game, frame, (first_id, second_id))
    metas.append(meta)


def build_cases(
    paths: list[Path],
    *,
    budget_ms: int,
    max_depth: int,
) -> tuple[str, list[CaseMeta], list[dict[str, Any]]]:
    lines: list[str] = []
    metas: list[CaseMeta] = []
    selected: list[dict[str, Any]] = []

    for game_index, path in enumerate(paths):
        game = json.loads(path.read_text())
        frames = game.get("frames", [])
        if len(frames) < 2 or len(frames[0].get("Snakes", [])) != 2:
            continue

        snake0_id = frames[0]["Snakes"][0]["ID"]
        snake1_id = frames[0]["Snakes"][1]["ID"]
        ids = (snake0_id, snake1_id)
        win_id = winner_id(frames, ids)
        selected.append(
            {
                "game_id": game.get("game_id", path.stem),
                "path": str(path),
                "snake0_id": snake0_id,
                "snake0_name": frames[0]["Snakes"][0].get("Name", snake0_id),
                "snake1_id": snake1_id,
                "snake1_name": frames[0]["Snakes"][1].get("Name", snake1_id),
                "winner_id": win_id,
            }
        )

        for frame, next_frame in zip(frames, frames[1:]):
            current = snakes_by_id(frame)
            nxt = snakes_by_id(next_frame)
            if not snake_alive(current.get(snake0_id)) or not snake_alive(current.get(snake1_id)):
                continue
            if snake0_id not in nxt or snake1_id not in nxt:
                continue
            move0 = head_move(current[snake0_id], nxt[snake0_id])
            move1 = head_move(current[snake1_id], nxt[snake1_id])
            if move0 is None or move1 is None:
                continue

            game_id = game.get("game_id", path.stem)
            turn = int(frame.get("Turn", 0))
            base = f"g{game_index}_t{turn}"
            add_case(
                lines,
                metas,
                game,
                frame,
                case_id=f"{base}_root",
                first_id=snake0_id,
                second_id=snake1_id,
                budget_ms=budget_ms,
                max_depth=max_depth,
                apply_first="none",
                apply_second="none",
                meta=CaseMeta(f"{base}_root", game_id, turn, "root", snake0_id, snake1_id, "", move0, move1, win_id, snake0_id, snake1_id),
            )

    return str(len(metas)) + "\n" + "\n".join(lines) + "\n", metas, selected


def compile_cli(output: Path, openmp: bool, native: bool) -> None:
    sources = [
        "tools/position_eval_batch_cli.c",
        "battlesnake/c-core/datatypes/coord.c",
        "battlesnake/c-core/datatypes/snake.c",
        "battlesnake/c-core/datatypes/board.c",
        "battlesnake/c-core/core/search_stats.c",
        "battlesnake/c-core/core/core_algorithms.c",
        "battlesnake/c-core/core/search_workspace.c",
        "battlesnake/c-core/core/search_state.c",
        "battlesnake/c-core/core/zobrist.c",
        "battlesnake/c-core/core/transposition_table.c",
        "battlesnake/c-core/core/position_eval.c",
    ]
    cmd = ["gcc", "-O3", "-std=c2x", "-D_POSIX_C_SOURCE=200809L", "-I", "battlesnake/c-core"]
    if native:
        cmd.extend(["-march=native", "-mtune=native"])
    if openmp:
        cmd.extend(["-DCORE_POSITION_EVAL_OPENMP", "-fopenmp"])
    cmd.extend([*sources, "-lm"])
    if openmp:
        cmd.append("-fopenmp")
    cmd.extend(["-o", str(output)])
    subprocess.run(cmd, check=True)


def read_results(path: Path) -> dict[str, dict[str, Any]]:
    with path.open(newline="") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        return {row["case_id"]: row for row in reader}


def pct(numerator: int, denominator: int) -> float:
    return 100.0 * numerator / denominator if denominator else 0.0


def summarize(
    metas: list[CaseMeta],
    results: dict[str, dict[str, Any]],
    output_dir: Path,
    *,
    support_threshold: float,
) -> None:
    root_rows: list[dict[str, Any]] = []
    move_rows: list[dict[str, Any]] = []

    for meta in metas:
        row = results.get(meta.case_id)
        if row is None or row.get("status") != "0":
            continue
        p = float(row["p"])
        confidence = float(row["confidence"])
        if meta.kind == "root":
            winner_p = None
            state_correct = None
            if meta.winner_id == meta.snake0_id:
                winner_p = p
                state_correct = p > 0.5
            elif meta.winner_id == meta.snake1_id:
                winner_p = 1.0 - p
                state_correct = p < 0.5
            root_rows.append(
                {
                    "game_id": meta.game_id,
                    "turn": meta.turn,
                    "snake0_id": meta.snake0_id,
                    "snake1_id": meta.snake1_id,
                    "winner_id": meta.winner_id or "",
                    "p_snake0": p,
                    "p_snake1": 1.0 - p,
                    "winner_probability": "" if winner_p is None else winner_p,
                    "state_correct": "" if state_correct is None else int(state_correct),
                    "confidence": confidence,
                    "nodes": row["nodes"],
                    "completed_depth": int(row.get("completed_depth", 0)),
                    "max_depth_started": int(row.get("max_depth_started", 0)),
                    "timed_out": row["timed_out"],
                    "snake0_up": float(row["first_up"]),
                    "snake0_down": float(row["first_down"]),
                    "snake0_left": float(row["first_left"]),
                    "snake0_right": float(row["first_right"]),
                    "snake1_up": float(row["second_up"]),
                    "snake1_down": float(row["second_down"]),
                    "snake1_left": float(row["second_left"]),
                    "snake1_right": float(row["second_right"]),
                }
            )

            first_policy = {move: float(row[f"first_{move}"]) for move in MOVES}
            second_policy = {move: float(row[f"second_{move}"]) for move in MOVES}
            for snake_index, snake_id, actual_move, policy in (
                (0, meta.snake0_id, meta.actual_move, first_policy),
                (1, meta.snake1_id, meta.candidate_move, second_policy),
            ):
                best = max(policy.values())
                actual_probability = policy.get(actual_move, 0.0)
                move_rows.append(
                    {
                        "game_id": meta.game_id,
                        "turn": meta.turn,
                        "snake_index": snake_index,
                        "snake_id": snake_id,
                        "actual_move": actual_move,
                        "policy_up": policy["up"],
                        "policy_down": policy["down"],
                        "policy_left": policy["left"],
                        "policy_right": policy["right"],
                        "policy_prob": actual_probability,
                        "support_hit": int(actual_probability >= support_threshold),
                        "top1_hit": int(actual_probability >= best - 1e-12),
                        "regret": best - actual_probability,
                        "nodes": row["nodes"],
                        "completed_depth": int(row.get("completed_depth", 0)),
                        "max_depth_started": int(row.get("max_depth_started", 0)),
                        "timed_out": row["timed_out"],
                    }
                )

    summaries: list[dict[str, Any]] = []
    game_ids = sorted({row["game_id"] for row in root_rows})
    for game_id in game_ids:
        roots = [row for row in root_rows if row["game_id"] == game_id]
        moves = [row for row in move_rows if row["game_id"] == game_id]
        known_roots = [row for row in roots if row["state_correct"] != ""]
        correct_roots = sum(int(row["state_correct"]) for row in known_roots)
        top1_correct = sum(int(row["top1_hit"]) for row in moves)
        support_correct = sum(int(row["support_hit"]) for row in moves)
        policy_probs = [float(row["policy_prob"]) for row in moves]
        regrets = [float(row["regret"]) for row in moves]
        winner_probs = [float(row["winner_probability"]) for row in known_roots if row["winner_probability"] != ""]
        confidences = [float(row["confidence"]) for row in roots]
        completed_depths = [int(row["completed_depth"]) for row in roots]
        max_depths_started = [int(row["max_depth_started"]) for row in roots]
        late_cutoff = int(max((row["turn"] for row in roots), default=0) * 0.8)
        late_probs = [float(row["winner_probability"]) for row in known_roots if row["winner_probability"] != "" and int(row["turn"]) >= late_cutoff]
        summaries.append(
            {
                "game_id": game_id,
                "turns": len(roots),
                "winner_id": roots[0]["winner_id"] if roots else "",
                "state_correct": correct_roots,
                "state_total": len(known_roots),
                "state_accuracy_pct": pct(correct_roots, len(known_roots)),
                "avg_winner_probability": sum(winner_probs) / len(winner_probs) if winner_probs else 0.0,
                "late_avg_winner_probability": sum(late_probs) / len(late_probs) if late_probs else 0.0,
                "avg_confidence": sum(confidences) / len(confidences) if confidences else 0.0,
                "avg_completed_depth": sum(completed_depths) / len(completed_depths) if completed_depths else 0.0,
                "max_depth_started": max(max_depths_started) if max_depths_started else 0,
                "top1_correct": top1_correct,
                "support_correct": support_correct,
                "move_total": len(moves),
                "top1_accuracy_pct": pct(top1_correct, len(moves)),
                "support_accuracy_pct": pct(support_correct, len(moves)),
                "avg_policy_prob": sum(policy_probs) / len(policy_probs) if policy_probs else 0.0,
                "avg_regret": sum(regrets) / len(regrets) if regrets else 0.0,
                "timeouts": sum(int(row["timed_out"]) for row in roots),
            }
        )

    for filename, rows in (
        ("root_scores.csv", root_rows),
        ("move_scores.csv", move_rows),
        ("summary.csv", summaries),
    ):
        with (output_dir / filename).open("w", newline="") as handle:
            if not rows:
                continue
            writer = csv.DictWriter(handle, fieldnames=list(rows[0].keys()))
            writer.writeheader()
            writer.writerows(rows)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--exports", default="exports", type=Path)
    parser.add_argument("--output-dir", default="exports/position_eval_analysis", type=Path)
    parser.add_argument("--sample-size", default=10, type=int)
    parser.add_argument("--seed", default=20260703, type=int)
    parser.add_argument("--budget-ms", default=20, type=int)
    parser.add_argument("--max-depth", default=2, type=int)
    parser.add_argument("--support-threshold", default=0.05, type=float)
    parser.add_argument("--openmp", action="store_true", help="compile the batch CLI with OpenMP root-cell parallelism")
    parser.add_argument("--native", action="store_true", help="compile the batch CLI for the current host CPU with -march=native -mtune=native")
    parser.add_argument("--threads", type=int, default=None, help="set OMP_NUM_THREADS for the batch CLI process")
    args = parser.parse_args()

    env = os.environ.copy()
    if args.threads is not None:
        if args.threads < 1:
            parser.error("--threads must be >= 1")
        env["OMP_NUM_THREADS"] = str(args.threads)
    elif args.openmp:
        env["OMP_NUM_THREADS"] = "1"

    paths = sorted(args.exports.glob("**/*.json"))
    rng = random.Random(args.seed)
    selected = rng.sample(paths, min(args.sample_size, len(paths)))
    args.output_dir.mkdir(parents=True, exist_ok=True)
    run_config = {
        "exports": str(args.exports),
        "output_dir": str(args.output_dir),
        "sample_size": args.sample_size,
        "seed": args.seed,
        "budget_ms": args.budget_ms,
        "max_depth": args.max_depth,
        "support_threshold": args.support_threshold,
        "openmp": args.openmp,
        "native": args.native,
        "threads": args.threads,
        "effective_omp_num_threads": env.get("OMP_NUM_THREADS"),
    }
    (args.output_dir / "run_config.json").write_text(json.dumps(run_config, indent=2) + "\n")

    batch_input, metas, selected_meta = build_cases(selected, budget_ms=args.budget_ms, max_depth=args.max_depth)
    (args.output_dir / "selected_games.json").write_text(json.dumps(selected_meta, indent=2))
    batch_path = args.output_dir / "position_eval_cases.txt"
    output_path = args.output_dir / "position_eval_raw.tsv"
    batch_path.write_text(batch_input)

    cli_path = args.output_dir / "position_eval_batch_cli"
    compile_cli(cli_path, args.openmp, args.native)
    with batch_path.open("rb") as stdin, output_path.open("wb") as stdout:
        subprocess.run([str(cli_path)], stdin=stdin, stdout=stdout, check=True, env=env)

    results = read_results(output_path)
    summarize(metas, results, args.output_dir, support_threshold=args.support_threshold)
    print(f"selected_games={len(selected_meta)} cases={len(metas)} output_dir={args.output_dir}")


if __name__ == "__main__":
    main()
