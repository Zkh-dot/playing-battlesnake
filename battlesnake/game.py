"""Board representation and immediate Battlesnake simulation helpers."""

from __future__ import annotations

from copy import deepcopy
from typing import Iterable

from .types import Coord, GameState, Move, Snake


MOVE_DELTAS: dict[Move, tuple[int, int]] = {
    Move.UP: (0, 1),
    Move.DOWN: (0, -1),
    Move.LEFT: (-1, 0),
    Move.RIGHT: (1, 0),
}


class Board:
    """A mutable-free snapshot of a Battlesnake board.

    The board stores snake bodies, food, hazards, and ruleset context, and can
    produce immediate safe moves or a one-turn simulated successor board.
    """

    def __init__(
        self,
        width: int,
        height: int,
        snakes: dict[str, Snake],
        food: Iterable[Coord] | None = None,
        hazards: Iterable[Coord] | None = None,
        ruleset_name: str = "standard",
        hazard_damage: int = 15,
    ) -> None:
        self.width = width
        self.height = height
        self.snakes = deepcopy(snakes)
        self.food = set(food or [])
        self.hazards = set(hazards or [])
        self.ruleset_name = ruleset_name
        self.hazard_damage = hazard_damage

    @classmethod
    def from_game_state(cls, state: GameState) -> "Board":
        """Build a board snapshot from a Battlesnake API game state."""

        settings = state.game.ruleset.settings
        hazard_damage = int(settings.get("hazardDamagePerTurn", 15))
        return cls(
            width=state.board.width,
            height=state.board.height,
            snakes={snake.id: snake for snake in state.board.snakes},
            food=state.board.food,
            hazards=state.board.hazards,
            ruleset_name=state.game.ruleset.name,
            hazard_damage=hazard_damage,
        )

    def in_bounds(self, coord: Coord) -> bool:
        """Return True when coord is inside the board."""

        return 0 <= coord.x < self.width and 0 <= coord.y < self.height

    def head(self, snake_id: str) -> Coord:
        """Return the current head coordinate for a snake id."""

        snake = self.snakes[snake_id]
        if snake.head is not None:
            return snake.head
        if not snake.body:
            raise KeyError(f"Snake {snake_id!r} has no body")
        return snake.body[0]

    def step(self, coord: Coord, move: Move) -> Coord:
        """Return the coordinate reached by applying move to coord."""

        dx, dy = MOVE_DELTAS[move]
        return Coord(x=coord.x + dx, y=coord.y + dy)

    def occupied(self, include_tails: bool = True) -> set[Coord]:
        """Return coordinates occupied by snake bodies."""

        coords: set[Coord] = set()
        for snake in self.snakes.values():
            body = snake.body if include_tails else snake.body[:-1]
            coords.update(body)
        return coords

    def is_safe(self, coord: Coord, snake_id: str) -> bool:
        """Return True if moving snake_id to coord avoids immediate death.

        Hazards are not treated as intrinsically unsafe because they only deal
        health damage. A hazard is unsafe here only if the snake cannot survive
        the next turn's damage.
        """

        if snake_id not in self.snakes or not self.in_bounds(coord):
            return False

        snake = self.snakes[snake_id]
        tail_vacates: set[Coord] = set()
        for other_id, other in self.snakes.items():
            if not other.body or self.ruleset_name == "constrictor":
                continue
            if other_id == snake_id:
                if coord not in self.food:
                    tail_vacates.add(other.body[-1])
                continue

            enemy_can_eat = any(self.step(self.head(other_id), move) in self.food for move in Move)
            if not enemy_can_eat:
                tail_vacates.add(other.body[-1])

        occupied = self.occupied(include_tails=True) - tail_vacates
        if coord in occupied:
            return False

        if coord in self.hazards and snake.health <= self.hazard_damage + 1:
            return False

        own_length = snake.length or len(snake.body)
        for other_id, other in self.snakes.items():
            if other_id == snake_id or not other.body:
                continue
            if coord in {self.step(self.head(other_id), move) for move in Move}:
                other_length = other.length or len(other.body)
                if other_length >= own_length:
                    return False

        return True

    def safe_moves(self, snake_id: str) -> list[Move]:
        """Return legal moves for snake_id that avoid immediate death."""

        if snake_id not in self.snakes:
            return []

        head = self.head(snake_id)
        return [move for move in Move if self.is_safe(self.step(head, move), snake_id)]

    def clone_and_apply(self, moves: dict[str, Move]) -> "Board":
        """Return a new board with all snakes advanced one simultaneous turn.

        The simulation resolves wall deaths, health loss, food growth,
        hazard damage, body collisions, and head-to-head collisions. Food that
        is not eaten remains in place; eaten food is removed without spawning a
        replacement.
        """

        next_snakes: dict[str, Snake] = {}
        new_heads: dict[str, Coord] = {}
        ate_food: set[str] = set()
        dead: set[str] = set()

        for snake_id, snake in self.snakes.items():
            move = moves.get(snake_id)
            if move is None:
                dead.add(snake_id)
                continue

            head = self.head(snake_id)
            new_head = self.step(head, move)
            new_heads[snake_id] = new_head

            health = snake.health - 1
            if new_head in self.hazards:
                health -= self.hazard_damage
            grew = new_head in self.food or self.ruleset_name == "constrictor"
            if new_head in self.food:
                health = 100
                ate_food.add(snake_id)

            body = [new_head, *snake.body]
            if not grew:
                body = body[:-1]

            next_snakes[snake_id] = snake.copy(
                update={
                    "health": health,
                    "head": new_head,
                    "body": body,
                    "length": len(body),
                },
                deep=True,
            )

            if not self.in_bounds(new_head) or health <= 0:
                dead.add(snake_id)

        body_cells: dict[Coord, list[str]] = {}
        for snake_id, snake in next_snakes.items():
            for coord in snake.body[1:]:
                body_cells.setdefault(coord, []).append(snake_id)

        for snake_id, head in new_heads.items():
            if snake_id in dead:
                continue
            if head in body_cells:
                dead.add(snake_id)

        head_cells: dict[Coord, list[str]] = {}
        for snake_id, head in new_heads.items():
            if snake_id not in dead:
                head_cells.setdefault(head, []).append(snake_id)

        for snake_ids in head_cells.values():
            if len(snake_ids) <= 1:
                continue
            max_length = max(next_snakes[snake_id].length or 0 for snake_id in snake_ids)
            winners = [
                snake_id
                for snake_id in snake_ids
                if (next_snakes[snake_id].length or 0) == max_length
            ]
            if len(winners) == 1:
                dead.update(snake_id for snake_id in snake_ids if snake_id != winners[0])
            else:
                dead.update(snake_ids)

        alive_snakes = {
            snake_id: snake for snake_id, snake in next_snakes.items() if snake_id not in dead
        }
        remaining_food = {coord for coord in self.food if coord not in new_heads.values()}

        return Board(
            width=self.width,
            height=self.height,
            snakes=alive_snakes,
            food=remaining_food,
            hazards=self.hazards,
            ruleset_name=self.ruleset_name,
            hazard_damage=self.hazard_damage,
        )
