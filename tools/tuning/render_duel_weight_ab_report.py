#!/usr/bin/env python3
"""Render the issue #46 Markdown report from committed machine evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
from collections import defaultdict
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.tuning.duel_weight_profiles import load_profile


PROFILE_PATHS = (
    ROOT / "configs/evaluation_weights/default.json",
    ROOT / "configs/evaluation_weights/tuned-opponent-pressure.json",
)


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _fmt(value: float, digits: int = 3) -> str:
    return f"{value:.{digits}f}"


def render_report(ab_path: Path, replay_path: Path) -> str:
    ab_path = ab_path.resolve()
    replay_path = replay_path.resolve()
    ab = json.loads(ab_path.read_text())
    replay = json.loads(replay_path.read_text())
    if ab.get("schema_version") != 3:
        raise ValueError("A/B evidence must use compact schema version 3")
    if replay.get("schema_version") != 1:
        raise ValueError("replay evidence must use schema version 1")
    summary = ab["summary"]
    before = summary["profiles"]["before"]
    after = summary["profiles"]["after"]
    paired = summary["paired_outcomes"]
    uncertainty = summary["paired_uncertainty"]
    settings = ab["settings"]
    environment = ab["environment"]
    profiles = {profile.status: profile for profile in map(load_profile, PROFILE_PATHS)}
    default = profiles["production-default"]
    candidate = profiles["candidate"]
    if ab["profiles"]["before"] != {"identifier": f"{default.name}@{default.version}", "sha256": default.sha256}:
        raise ValueError("A/B default metadata does not match generated registry source")
    if ab["profiles"]["after"] != {"identifier": f"{candidate.name}@{candidate.version}", "sha256": candidate.sha256}:
        raise ValueError("A/B candidate metadata does not match generated registry source")

    errors_pass = all(
        metrics["search_errors"] == 0 and metrics["audit_errors"] == 0
        for metrics in (before, after)
    )
    violations_pass = after["policy_violations"] <= before["policy_violations"]
    risks_pass = after["structural_risk_selections"] <= before["structural_risk_selections"]
    survival_pass = (
        after["terminal_survivals"] >= before["terminal_survivals"]
        and after["alive_at_cap"] >= before["alive_at_cap"]
    )
    lower, upper = uncertainty["after_sweep_share_wilson_95"]
    paired_pass = uncertainty["decisive_pairs"] > 0 and lower > 0.5
    default_p99 = before["latency_ms"]["p99"]
    candidate_p99 = after["latency_ms"]["p99"]
    latency_ratio = candidate_p99 / default_p99 if default_p99 else float("inf")
    latency_pass = candidate_p99 <= settings["time_budget_ms"] and latency_ratio <= 1.10
    gate_pass = all((errors_pass, violations_pass, risks_pass, survival_pass, paired_pass, latency_pass))
    decision = "promote in a separate PR" if gate_pass else "inconclusive — do not promote"

    replay_groups: dict[tuple[object, ...], list[dict[str, object]]] = defaultdict(list)
    for record in replay["records"]:
        replay_groups[(record["game_id"], record["turn"], record["recorded_move"], record["profile"])].append(record)
    replay_errors = sum(record["error"] is not None for record in replay["records"])
    replay_timeouts = sum(bool(record["timed_out"]) for record in replay["records"])
    replay_risks = sum(bool(record["structural_risk"]) for record in replay["records"])
    replay_violations = sum(bool(record["policy_violation"]) for record in replay["records"])
    replay_rows: list[str] = []
    positions = sorted({(key[0], key[1], key[2]) for key in replay_groups})
    identifiers = (f"{default.name}@{default.version}", f"{candidate.name}@{candidate.version}")
    for game_id, turn, recorded in positions:
        cells = []
        for identifier in identifiers:
            rows = replay_groups[(game_id, turn, recorded, identifier)]
            moves = sorted({str(row["move"]) for row in rows})
            depths = [int(row["depth"]) for row in rows]
            cells.append(f"{'/'.join(moves)} / {min(depths)}–{max(depths)}")
        replay_rows.append(f"| `{str(game_id)[:8]}…` / {turn} | {recorded} | {cells[0]} | {cells[1]} |")

    raw = ab["raw"]
    latency_samples = sum(len(raw["measurements"][profile]["latency_ms"]) for profile in ("before", "after"))
    lines = [
        "# Issue #46 duel weight promotion evidence", "", "## Decision", "",
        f"**{decision}.** The generated production default remains `{default.name}@{default.version}`; this PR only makes the candidate selectable and auditable, and any promotion requires a separate PR.", "",
        f"The candidate won {after['wins']} games to the default's {before['wins']}, with {after['draws']} draws. At the paired-board unit, the candidate swept {paired['after_sweeps']}, the default swept {paired['before_sweeps']}, {paired['split_pairs']} split, and {paired['pairs_with_draw']} contained a draw. The candidate decisive-sweep share was {_fmt(100 * uncertainty['after_sweep_share'], 2)}% (Wilson 95% {_fmt(100 * lower, 2)}%–{_fmt(100 * upper, 2)}%; exact two-sided sign-test p={uncertainty['two_sided_p_value']:.4f}).", "",
        "## Frozen experiment", "",
        f"- Experiment input commit: `{environment['experiment_input_commit']}`",
        f"- Host: `{environment['host']}`, Linux `{environment['kernel']}`, `{environment['architecture']}`",
        f"- CPU: {environment['cpu']} ({environment['logical_cpus']} logical CPUs)",
        f"- Python: {environment['python']}", f"- Compiler: {environment['compiler']}",
        f"- Tool-recorded wall time: {_fmt(environment['wall_seconds'], 2)} s; max RSS: {environment['max_rss_kb']:,} KiB", "",
        "| Role | Profile | Canonical weights SHA-256 | Source-file SHA-256 |", "| --- | --- | --- | --- |",
        f"| default | `{default.name}@{default.version}` | `{default.sha256}` | `{_sha256(PROFILE_PATHS[0])}` |",
        f"| candidate | `{candidate.name}@{candidate.version}` | `{candidate.sha256}` | `{_sha256(PROFILE_PATHS[1])}` |", "",
        "```bash", "python3 tools/tuning/compare_weights_matches.py \\",
        "  --before-weights configs/evaluation_weights/default.json \\",
        "  --after-weights configs/evaluation_weights/tuned-opponent-pressure.json \\",
        f"  --seed {settings['seed']} --scenario-count {settings['scenario_count']} --fixed-depth {settings['fixed_depth']} \\",
        f"  --time-budget-ms {settings['time_budget_ms']} --max-turns {settings['max_turns']} \\",
        "  --output docs/evidence/issue-46-duel-weight-ab.json \\",
        "  --markdown-output /tmp/issue-46-duel-weight-ab.generated.md", "```", "",
        f"Each of {settings['scenario_count']} seeded Standard boards was played twice with physical sides swapped. Search order was also paired: each profile was evaluated first exactly once per board. Both searches received the same unchanged board snapshot. Each profile played {after['physical_side_games']['0']} games from physical side 0 and {after['physical_side_games']['1']} from side 1.", "",
        "## A/B results", "", "| Metric | Default | Candidate |", "| --- | ---: | ---: |",
        f"| wins / losses / draws | {before['wins']} / {before['losses']} / {before['draws']} | {after['wins']} / {after['losses']} / {after['draws']} |",
        f"| terminal survivals | {before['terminal_survivals']} | {after['terminal_survivals']} |",
        f"| alive at {settings['max_turns']}-turn cap | {before['alive_at_cap']} | {after['alive_at_cap']} |",
        f"| final length total | {before['final_length_total']} | {after['final_length_total']} |",
        f"| search selections | {before['move_count']} | {after['move_count']} |",
        f"| search errors / audit errors | {before['search_errors']} / {before['audit_errors']} | {after['search_errors']} / {after['audit_errors']} |",
        f"| search timeouts | {before['search_timeouts']} | {after['search_timeouts']} |",
        f"| structural-risk selections | {before['structural_risk_selections']} | {after['structural_risk_selections']} |",
        f"| independent policy violations | {before['policy_violations']} | {after['policy_violations']} |",
        f"| native elapsed p50 / p95 / p99 / max, ms | {_fmt(before['latency_ms']['p50'])} / {_fmt(before['latency_ms']['p95'])} / {_fmt(default_p99)} / {_fmt(before['latency_ms']['max'])} | {_fmt(after['latency_ms']['p50'])} / {_fmt(after['latency_ms']['p95'])} / {_fmt(candidate_p99)} / {_fmt(after['latency_ms']['max'])} |", "",
        f"Shared match duration was {summary['shared_match_duration']['turns_total']:,} turns total ({_fmt(summary['shared_match_duration']['turns_mean'], 2)} mean). It is not attributed as profile survival. Profile survival uses terminal survival, alive-at-cap, and final state only.", "",
        f"The compact artifact retains {len(raw['matches'])} match outcome/final-state rows and {latency_samples:,} raw native latency samples. Sparse event arrays retain identifiers for every timeout, search error, audit error, structural risk, and policy violation. All headline metrics are recomputed from these raw structures.", "",
        "## Replay-risk diagnostics", "",
        f"The replay run used {replay['settings']['budget_ms']} ms, {replay['settings']['repeats']} repeats, and both named profiles: {len(replay['records'])} records, {replay_errors} errors, {replay_timeouts} timeouts, {replay_risks} structural risks, and {replay_violations} policy violations.", "",
        "| Game / turn | Recorded move | Default move / depth range | Candidate move / depth range |", "| --- | --- | --- | --- |",
        *replay_rows, "",
        "These replay positions are diagnostics, not universal expected-move assertions.", "",
        "## Predeclared gate", "", "| Criterion | Result |", "| --- | --- |",
        f"| zero search/audit errors | {'pass' if errors_pass else 'fail'}: default {before['search_errors']}/{before['audit_errors']}, candidate {after['search_errors']}/{after['audit_errors']} |",
        f"| no increase in policy violations | {'pass' if violations_pass else 'fail'}: {before['policy_violations']} vs {after['policy_violations']} |",
        f"| no material structural-risk increase | {'pass' if risks_pass else 'fail'}: {before['structural_risk_selections']} vs {after['structural_risk_selections']} |",
        f"| non-inferior terminal/alive-at-cap survival | {'pass descriptively' if survival_pass else 'fail'}: terminal {before['terminal_survivals']} vs {after['terminal_survivals']}; cap {before['alive_at_cap']} vs {after['alive_at_cap']} |",
        f"| positive paired signal with uncertainty | {'pass' if paired_pass else '**not established**'}: Wilson lower bound {_fmt(100 * lower, 2)}%, p={uncertainty['two_sided_p_value']:.4f} |",
        f"| candidate p99 ≤{settings['time_budget_ms']} ms and ≤110% of default | {'pass' if latency_pass else 'fail'}: {_fmt(candidate_p99)} ms and {_fmt(100 * latency_ratio, 2)}% |", "",
        f"Overall evidence decision: **{decision}**.", "",
        "## Limitations", "", "- One deterministic seed over generated Standard boards is not the live ladder distribution.",
        "- Fixed depth 3 is reproducible but does not model production iterative-deepening depth distribution.",
        f"- Split pairs dominate ({paired['split_pairs']}/{summary['pairs']}); treating individual games as independent would be pseudo-replication.",
        "- The Wilson interval is for decisive sweeps and excludes split/draw-containing pairs.",
        "- Four targeted replay positions do not prove universal move optimality.", "",
        "Raw evidence:", "", f"- `{ab_path.relative_to(ROOT)}` — SHA-256 `{_sha256(ab_path)}`",
        f"- `{replay_path.relative_to(ROOT)}` — SHA-256 `{_sha256(replay_path)}`", "",
    ]
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ab", type=Path, required=True)
    parser.add_argument("--replays", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.write_text(render_report(args.ab, args.replays))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
