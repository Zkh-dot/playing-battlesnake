from __future__ import annotations

import json
import zipfile
from dataclasses import dataclass
from pathlib import Path
from typing import Iterator


@dataclass(frozen=True)
class PlayerMeta:
    rank: int
    slug: str
    display: str


def load_manifest(archive_path: Path) -> dict[str, object]:
    with zipfile.ZipFile(archive_path) as archive:
        return json.loads(archive.read("MANIFEST.json"))


def player_rank_by_display(manifest: dict[str, object]) -> dict[str, PlayerMeta]:
    players = manifest.get("selected_players", [])
    result: dict[str, PlayerMeta] = {}
    if not isinstance(players, list):
        return result
    for raw in players:
        if not isinstance(raw, dict):
            continue
        display = str(raw.get("display") or raw.get("slug") or "")
        if not display:
            continue
        result[display] = PlayerMeta(
            rank=int(raw.get("rank", 0)),
            slug=str(raw.get("slug", display)),
            display=display,
        )
    return result


def iter_replay_exports(archive_path: Path) -> Iterator[tuple[str, dict[str, object]]]:
    with zipfile.ZipFile(archive_path) as archive:
        names = sorted(
            name
            for name in archive.namelist()
            if name.endswith(".json") and name != "MANIFEST.json" and not name.endswith("/")
        )
        for name in names:
            data = json.loads(archive.read(name))
            if isinstance(data, dict) and "game" in data and "frames" in data:
                yield name, data
