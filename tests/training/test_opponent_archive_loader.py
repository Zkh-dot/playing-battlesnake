from __future__ import annotations

import json
import tempfile
import unittest
import zipfile
from pathlib import Path

from battlesnake.training.opponent_model.archive_loader import (
    PlayerMeta,
    iter_replay_exports,
    load_manifest,
    player_rank_by_display,
)


class OpponentArchiveLoaderTests(unittest.TestCase):
    def test_load_manifest_and_replays_from_zip(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            archive = Path(tmp) / "games.zip"
            with zipfile.ZipFile(archive, "w") as zf:
                zf.writestr(
                    "MANIFEST.json",
                    json.dumps(
                        {
                            "leaderboard_url": "https://play.battlesnake.com/leaderboard/standard",
                            "selected_players": [
                                {"rank": 1, "slug": "alpha", "display": "Alpha"},
                                {"rank": 2, "slug": "beta", "display": "Beta"},
                            ],
                        }
                    ),
                )
                replay_b = {"game_id": "game-b", "game": {"ID": "game-b"}, "frames": []}
                replay_a = {"game_id": "game-a", "game": {"ID": "game-a"}, "frames": []}
                zf.writestr("game-b.json", json.dumps(replay_b))
                zf.writestr("game-a.json", json.dumps(replay_a))

            manifest = load_manifest(archive)
            ranks = player_rank_by_display(manifest)
            exports = list(iter_replay_exports(archive))

        self.assertEqual(ranks["Alpha"], PlayerMeta(rank=1, slug="alpha", display="Alpha"))
        self.assertEqual([name for name, _ in exports], ["game-a.json", "game-b.json"])
        self.assertEqual(exports[0][1]["game_id"], "game-a")

    def test_player_rank_by_display_skips_unparseable_ranks(self) -> None:
        manifest: dict[str, object] = {
            "selected_players": [
                {"rank": None, "slug": "none-rank", "display": "None Rank"},
                {"rank": "bad", "slug": "bad-rank", "display": "Bad Rank"},
                {"rank": "3", "slug": "slug-rank"},
                {"slug": "missing-rank"},
            ]
        }

        ranks = player_rank_by_display(manifest)

        self.assertNotIn("None Rank", ranks)
        self.assertNotIn("Bad Rank", ranks)
        self.assertEqual(ranks["slug-rank"], PlayerMeta(rank=3, slug="slug-rank", display="slug-rank"))
        self.assertEqual(
            ranks["missing-rank"],
            PlayerMeta(rank=0, slug="missing-rank", display="missing-rank"),
        )


if __name__ == "__main__":
    unittest.main()
