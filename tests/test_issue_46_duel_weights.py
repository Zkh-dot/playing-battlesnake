from __future__ import annotations

import json
import math
import subprocess
import sys
from pathlib import Path

import pytest

from battlesnake.battlesnake_native import Board, Coord, Snake, duel_weight_profiles, evaluate
from tools.tuning.duel_weight_profiles import (
    WEIGHT_KEYS,
    DuelWeightProfile,
    canonical_weights_sha256,
    load_profile,
    validate_profiles,
)


ROOT = Path(__file__).resolve().parents[1]
CONFIG_DIR = ROOT / "configs" / "evaluation_weights"
PROFILE_PATHS = [CONFIG_DIR / "default.json", CONFIG_DIR / "tuned-opponent-pressure.json"]
ENVELOPE_KEYS = {"schema_version", "name", "version", "status", "weights"}


def raw_default() -> dict[str, object]:
    return json.loads(PROFILE_PATHS[0].read_text())


def test_profiles_have_strict_complete_envelopes() -> None:
    profiles = [load_profile(path) for path in PROFILE_PATHS]
    assert [profile.status for profile in profiles] == ["production-default", "candidate"]
    for path, profile in zip(PROFILE_PATHS, profiles, strict=True):
        raw = json.loads(path.read_text())
        assert set(raw) == ENVELOPE_KEYS
        assert raw["schema_version"] == 1
        assert tuple(profile.weights) == WEIGHT_KEYS
        assert all(isinstance(value, (int, float)) and not isinstance(value, bool) for value in profile.weights.values())
        assert all(math.isfinite(value) for value in profile.weights.values())
        assert profile.name and profile.version
        assert canonical_weights_sha256(profile.weights) == profile.sha256


@pytest.mark.parametrize(
    ("mutate", "match"),
    [
        (lambda value: value.update(extra=1), "envelope"),
        (lambda value: value.pop("version"), "envelope"),
        (lambda value: value.update(schema_version=2), "schema_version"),
        (lambda value: value.update(name="bad name"), "name"),
        (lambda value: value.update(status="default"), "status"),
        (lambda value: value["weights"].update(extra=1.0), "weight keys"),
        (lambda value: value["weights"].pop("base"), "weight keys"),
        (lambda value: value["weights"].update(base=True), "number"),
        (lambda value: value["weights"].update(base="500"), "number"),
    ],
)
def test_profile_validation_rejects_invalid_data(tmp_path: Path, mutate, match: str) -> None:
    raw = raw_default()
    mutate(raw)
    path = tmp_path / "invalid.json"
    path.write_text(json.dumps(raw))
    with pytest.raises(ValueError, match=match):
        load_profile(path)


@pytest.mark.parametrize(
    ("duplicate", "match"),
    [
        ('"name": "shadowed",', "duplicate JSON field: name"),
        ('"weights": {"base": 999.0,', "duplicate JSON field: base"),
    ],
)
def test_profile_loader_rejects_duplicate_json_fields(
    tmp_path: Path,
    duplicate: str,
    match: str,
) -> None:
    raw = PROFILE_PATHS[0].read_text()
    if duplicate.startswith('"weights"'):
        raw = raw.replace('"weights": {', duplicate, 1)
    else:
        raw = raw.replace("{", "{" + duplicate, 1)
    path = tmp_path / "duplicate.json"
    path.write_text(raw)

    with pytest.raises(ValueError, match=match):
        load_profile(path)


def test_programmatic_nonfinite_and_registry_invariants_are_rejected() -> None:
    valid = load_profile(PROFILE_PATHS[0])
    invalid = DuelWeightProfile(
        schema_version=1,
        name="not-finite",
        version="1",
        status="candidate",
        weights={**valid.weights, "base": math.inf},
    )
    with pytest.raises(ValueError, match="finite"):
        validate_profiles([valid, invalid])
    with pytest.raises(ValueError, match="duplicate"):
        validate_profiles([valid, valid])
    with pytest.raises(ValueError, match="exactly one production-default"):
        validate_profiles([load_profile(PROFILE_PATHS[1])])


def test_generator_is_current() -> None:
    result = subprocess.run(
        [sys.executable, "tools/tuning/generate_duel_weight_profiles.py", "--check"],
        cwd=ROOT,
        text=True,
        capture_output=True,
    )
    assert result.returncode == 0, result.stderr or result.stdout


def test_native_registry_matches_validated_sources_field_for_field() -> None:
    expected = {(profile.name, profile.version): profile for profile in map(load_profile, PROFILE_PATHS)}
    native = duel_weight_profiles()
    assert len(native) == len(expected)
    for record in native:
        profile = expected[(record["name"], record["version"])]
        assert record["schema_version"] == profile.schema_version
        assert record["status"] == profile.status
        assert record["sha256"] == profile.sha256
        assert tuple(record["weights"]) == WEIGHT_KEYS
        assert record["weights"] == profile.weights
    default = next(record for record in native if record["status"] == "production-default")
    assert all(default["weights"][key] == 0.0 for key in (
        "opponent_reachable_space", "territory_delta", "opponent_safe_moves",
        "opponent_low_health_food_denial",
    ))


def test_generated_profile_has_native_evaluation_parity() -> None:
    board = Board(
        7,
        7,
        [
            Snake("us", "us", 73, [Coord(2, 2), Coord(2, 1), Coord(1, 1)]),
            Snake("them", "them", 41, [Coord(5, 4), Coord(5, 3), Coord(4, 3)]),
        ],
        food=[Coord(3, 2)],
    )
    native_by_id = {(p["name"], p["version"]): p for p in duel_weight_profiles()}
    for path in PROFILE_PATHS:
        profile = load_profile(path)
        native_weights = native_by_id[profile.identifier]["weights"]
        assert evaluate(board, "us", native_weights) == evaluate(board, "us", dict(profile.weights))
