from __future__ import annotations

import hashlib
import json
import math
import re
from dataclasses import dataclass
from pathlib import Path
from types import MappingProxyType
from typing import Iterable, Mapping


WEIGHT_KEYS = (
    "terminal_win",
    "terminal_loss",
    "base",
    "health",
    "length",
    "reachable_space",
    "safe_moves",
    "center",
    "food",
    "low_health_food",
    "low_health_threshold",
    "hazard_damage",
    "hazard",
    "length_advantage",
    "adjacent_equal_or_longer_penalty",
    "adjacent_shorter_bonus",
    "opponent_reachable_space",
    "territory_delta",
    "opponent_safe_moves",
    "opponent_low_health_food_denial",
)
ENVELOPE_KEYS = frozenset({"schema_version", "name", "version", "status", "weights"})
VALID_STATUSES = frozenset({"production-default", "candidate"})
_TOKEN = re.compile(r"^[a-z0-9][a-z0-9._-]*$")


@dataclass(frozen=True)
class DuelWeightProfile:
    schema_version: int
    name: str
    version: str
    status: str
    weights: Mapping[str, float]

    def __post_init__(self) -> None:
        object.__setattr__(self, "weights", MappingProxyType(dict(self.weights)))

    @property
    def identifier(self) -> tuple[str, str]:
        return self.name, self.version

    @property
    def sha256(self) -> str:
        return canonical_weights_sha256(self.weights)

    def to_dict(self) -> dict[str, object]:
        return {
            "schema_version": self.schema_version,
            "name": self.name,
            "version": self.version,
            "status": self.status,
            "weights": dict(self.weights),
        }


def _validated_profile(data: object, source: str) -> DuelWeightProfile:
    if not isinstance(data, dict) or set(data) != ENVELOPE_KEYS:
        raise ValueError(f"{source}: envelope keys must be exactly {sorted(ENVELOPE_KEYS)}")
    if data["schema_version"] != 1 or isinstance(data["schema_version"], bool):
        raise ValueError(f"{source}: schema_version must be 1")
    for field in ("name", "version"):
        value = data[field]
        if not isinstance(value, str) or not _TOKEN.fullmatch(value):
            raise ValueError(f"{source}: {field} must be a non-empty lowercase audit token")
    status = data["status"]
    if not isinstance(status, str) or status not in VALID_STATUSES:
        raise ValueError(f"{source}: status must be one of {sorted(VALID_STATUSES)}")
    raw_weights = data["weights"]
    if not isinstance(raw_weights, dict) or set(raw_weights) != set(WEIGHT_KEYS):
        raise ValueError(f"{source}: weight keys must be exactly {list(WEIGHT_KEYS)}")
    weights: dict[str, float] = {}
    for key in WEIGHT_KEYS:
        value = raw_weights[key]
        if not isinstance(value, (int, float)) or isinstance(value, bool):
            raise ValueError(f"{source}: weight {key} must be a JSON number")
        value = float(value)
        if not math.isfinite(value):
            raise ValueError(f"{source}: weight {key} must be finite")
        weights[key] = value
    return DuelWeightProfile(1, data["name"], data["version"], status, weights)


def validate_profile(profile: DuelWeightProfile, source: str = "profile") -> DuelWeightProfile:
    return _validated_profile(profile.to_dict(), source)


def load_profile(path: Path) -> DuelWeightProfile:
    try:
        data = json.loads(path.read_text())
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"{path}: invalid profile: {error}") from error
    return _validated_profile(data, str(path))


def validate_profiles(profiles: Iterable[DuelWeightProfile]) -> tuple[DuelWeightProfile, ...]:
    validated = tuple(validate_profile(profile) for profile in profiles)
    identifiers = [profile.identifier for profile in validated]
    if len(set(identifiers)) != len(identifiers):
        raise ValueError("profile registry contains a duplicate name/version identifier")
    if sum(profile.status == "production-default" for profile in validated) != 1:
        raise ValueError("profile registry must have exactly one production-default")
    return validated


def canonical_weights_json(weights: Mapping[str, float]) -> str:
    validated = _validated_profile(
        {
            "schema_version": 1,
            "name": "canonical",
            "version": "1",
            "status": "candidate",
            "weights": dict(weights),
        },
        "weights",
    )
    return json.dumps(dict(validated.weights), separators=(",", ":"), ensure_ascii=True, allow_nan=False)


def canonical_weights_sha256(weights: Mapping[str, float]) -> str:
    return hashlib.sha256(canonical_weights_json(weights).encode("ascii")).hexdigest()
