#!/usr/bin/env python3
"""Analyze Standard FFA decision JSONL telemetry."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from statistics import median
from typing import Any


def load_records(path: Path) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if stripped:
            records.append(json.loads(stripped))
    return records


def summarize(records: list[dict[str, Any]]) -> str:
    decisions = [record for record in records if record.get("type") == "decision"]
    by_game: dict[str, list[dict[str, Any]]] = {}
    for record in decisions:
        by_game.setdefault(str(record.get("game_id", "")), []).append(record)

    lines = [
        "game_id turns fallback_rate chosen_safe_avg reachable_avg contested_entry_rate latency_p50 latency_p95 latency_p99",
    ]
    for game_id in sorted(by_game):
        rows = sorted(by_game[game_id], key=lambda record: int(record.get("turn", 0)))
        fallback_rate = _average(1.0 if row.get("fallback_used") else 0.0 for row in rows)
        chosen_candidates = [_chosen_candidate(row) for row in rows]
        chosen_safe_avg = _average(
            float(candidate.get("immediate_safe_move_count_after", 0))
            for candidate in chosen_candidates
            if candidate is not None
        )
        reachable_avg = _average(
            float(candidate.get("immediate_reachable_space", 0))
            for candidate in chosen_candidates
            if candidate is not None
        )
        contested_rate = _average(1.0 if _entered_contested_cell(row) else 0.0 for row in rows)
        latencies = [float(row.get("latency_ms", 0.0)) for row in rows]
        lines.append(
            f"{game_id} {len(rows)} {fallback_rate:.3f} {chosen_safe_avg:.2f} "
            f"{reachable_avg:.2f} {contested_rate:.3f} {_percentile(latencies, 50):.3f} "
            f"{_percentile(latencies, 95):.3f} {_percentile(latencies, 99):.3f}"
        )

    game_end_rows = [record for record in records if record.get("type") == "game_end"]
    if game_end_rows:
        lines.append("")
        lines.append("game_end game_id turn death_cause")
        for record in sorted(game_end_rows, key=lambda row: (str(row.get("game_id", "")), int(row.get("turn", 0)))):
            lines.append(f"game_end {record.get('game_id')} {record.get('turn')} {record.get('death_cause')}")
    return "\n".join(lines)


def turn_table(records: list[dict[str, Any]], game_id: str, turn: int) -> str:
    for record in records:
        if record.get("type") == "decision" and record.get("game_id") == game_id and int(record.get("turn", -1)) == turn:
            return _format_turn(record)
    raise SystemExit(f"no decision record found for game_id={game_id!r} turn={turn}")


def _format_turn(record: dict[str, Any]) -> str:
    lines = [
        f"game_id={record.get('game_id')} turn={record.get('turn')} chosen={record.get('chosen_move')} latency_ms={float(record.get('latency_ms', 0.0)):.3f}",
        "move eligible death_class safe_after reachable score expected worst scenarios terms",
    ]
    for candidate in record.get("candidates", []):
        if not isinstance(candidate, dict):
            continue
        terms = candidate.get("terms") or {}
        terms_text = ",".join(f"{key}={float(value):.2f}" for key, value in sorted(terms.items()))
        lines.append(
            f"{candidate.get('move')} {candidate.get('eligible')} {candidate.get('death_class')} "
            f"{candidate.get('immediate_safe_move_count_after')} {candidate.get('immediate_reachable_space')} "
            f"{_num(candidate.get('score'))} {_num(candidate.get('expected'))} {_num(candidate.get('worst'))} "
            f"{candidate.get('scenario_count')} {terms_text}"
        )
    return "\n".join(lines)


def _chosen_candidate(record: dict[str, Any]) -> dict[str, Any] | None:
    chosen_move = record.get("chosen_move")
    for candidate in record.get("candidates", []):
        if isinstance(candidate, dict) and candidate.get("move") == chosen_move:
            return candidate
    return None


def _entered_contested_cell(record: dict[str, Any]) -> bool:
    chosen = _chosen_candidate(record)
    if chosen is None:
        return False
    target = chosen.get("target")
    if not isinstance(target, dict):
        return False
    target_pair = (target.get("x"), target.get("y"))
    for _opponent_id, priors in (record.get("opponent_priors") or {}).items():
        for row in priors:
            if not isinstance(row, dict) or row.get("probability", 0.0) <= 0:
                continue
            # The log stores priors, not opponent target cells; forced h2h risk
            # is already reflected in a head_to_head death class.
            if chosen.get("death_class") == "head_to_head_losing" and target_pair:
                return True
    return False


def _average(values: Any) -> float:
    data = list(values)
    return sum(data) / len(data) if data else 0.0


def _percentile(values: list[float], percentile: int) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    if percentile == 50:
        return float(median(ordered))
    index = max(0, min(len(ordered) - 1, int((percentile / 100.0) * len(ordered)) - 1))
    return ordered[index]


def _num(value: object) -> str:
    return "None" if value is None else f"{float(value):.2f}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path, help="Decision JSONL log")
    parser.add_argument("--game-id", help="Print one turn's candidate table for this game")
    parser.add_argument("--turn", type=int, help="Print one turn's candidate table")
    parser.add_argument("--replay", type=Path, help="Reserved for exported replay context")
    args = parser.parse_args()

    records = load_records(args.log)
    if args.game_id is not None or args.turn is not None:
        if args.game_id is None or args.turn is None:
            parser.error("--game-id and --turn must be provided together")
        print(turn_table(records, args.game_id, args.turn))
    else:
        print(summarize(records))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
