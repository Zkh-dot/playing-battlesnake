"""Training helpers for Battlesnake experiments."""

__all__ = ["play_game"]


def __getattr__(name: str):
    if name == "play_game":
        from battlesnake.training.self_play import play_game

        return play_game
    raise AttributeError(name)
