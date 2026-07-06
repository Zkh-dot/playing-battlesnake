# Standard FFA Gameplay Strategy Spec

## Purpose

This document specifies the intended Standard free-for-all gameplay strategy. It
is not an implementation plan. It defines what the snake should optimize, which
signals it should use, which signals already exist in the codebase, and how the
move-scoring formula should be tuned.

The core idea is:

1. Never let a learned model override deterministic safety.
2. Use deterministic board metrics to keep the snake alive and preserve space.
3. Use opponent priors to evaluate likely opponent responses at the root.
4. Optimize a shallow, risk-aware utility function against real gameplay
   outcomes, not against model accuracy alone.

The Standard FFA strategy should be a general-purpose survival and placement
strategy. It should not be an aggressive duel strategy copied into a four-snake
game, and it should not choose moves directly from the opponent model.

## Delivery Model

Production runs the native C server (`/app/battlesnake-server`, see
`docs/runbooks/battlesnake-deploy.md`). The native server currently routes
two-snake standard/solo games to duel minimax and everything else to a
first-safe fallback.

The Standard FFA strategy is developed and validated on a separate **dev
snake** first:

1. The dev snake is the Python FastAPI app (`battlesnake/main.py`), deployed
   with its own snake identity at `https://ya.sergeiscv.ru/test-snake/`. All
   heavy primitives it uses are native C calls, so the Python layer is a
   coordination loop, not the bottleneck.
2. Strategy iteration — formulas, scoring terms, weights, priors — happens on
   the dev snake, judged by arena results and per-move telemetry.
3. Only after the strategy is frozen by arena evidence is it ported to
   `c-core` and merged into the production native server, with Python-vs-C
   parity tests as the gate.

The Python `StrategyStandard` is therefore a development vehicle, not the
production endpoint. Nothing in this spec assumes the learned model or the
Python runtime is available in the production request path.

## Decision Contract

For every turn, the strategy chooses one move from:

```text
up, down, left, right
```

The decision is made by scoring each candidate move for our snake:

```text
selected_move = argmax_m StandardScore(m)
```

Only moves that survive hard safety gates are eligible. If all moves fail hard
gates, the strategy returns the least-bad move according to immediate death
classification and reachable space.

The scoring unit is the root turn, not a deep full-width game tree. Standard FFA
has too many simultaneous opponent combinations for naive exhaustive search to
be the default path. The planned deepening path is relevance-pruned shallow
search (see Joint Response Sampling), not wider joint enumeration.

## Strategic Priorities

The strategy should optimize these priorities in this order:

1. **Do not die immediately.**
   Avoid walls, bodies, self-collision, fatal hazards, and known losing
   head-to-head collisions.

2. **Preserve escape space.**
   Prefer moves that leave enough reachable cells and future safe moves. A move
   into a larger area is usually better than a move into a pocket, even if both
   are immediately safe.

3. **Avoid high-probability opponent collisions.**
   Use opponent move priors to recognize contested cells, likely head zones, and
   likely body-blocking moves.

4. **Eat when health makes food valuable.**
   Food is valuable when health is low, when route risk is acceptable, or when
   growth changes head-to-head outcomes. Food is not automatically valuable when
   it drags us into a trap.

5. **Improve placement, not only survival turns.**
   In FFA, outliving one snake matters. The strategy should accept controlled
   pressure when it increases expected placement, but only after hard safety and
   space constraints are satisfied.

6. **Exploit shorter snakes conservatively.**
   A length advantage makes head pressure possible, but the strategy should not
   chase kills through narrow corridors unless the expected downside is bounded.

## Existing Signals

### Already Available From Python Board API

These are exposed by the C-backed `Board` object used by Python strategy code:

| Signal | Source | Notes |
|---|---|---|
| Board dimensions | `board.width`, `board.height` | Direct scalar features. |
| Snake map | `board.snakes` | Includes id, health, length/body/head. |
| Food cells | `board.food` | Available as coordinate set. |
| Hazard cells | `board.hazards` | Available as coordinate set. |
| Hazard damage | `board.hazard_damage` | Relevant for Royale too, but Standard can keep the metric generic. |
| Current head | `board.head(snake_id)` | Native wrapper. |
| Step coordinate | `board.step(coord, move)` | Native wrapper. |
| In-bounds check | `board.in_bounds(coord)` | Native wrapper. |
| Immediate safety | `board.is_safe(coord, snake_id)` | Native wrapper. |
| Immediate safe moves | `board.safe_moves(snake_id)` | Native wrapper. |
| Occupied cells | `board.occupied(include_tails=...)` | Native wrapper. |
| One-turn simulation | `board.clone_and_apply({id: move})` | Native simultaneous move application. |

### Already Available From Native Core Wrappers

These functions already exist and should be reused before adding duplicate
Python logic:

| Signal | Source | Notes |
|---|---|---|
| Reachable space | `battlesnake.core.flood_fill.reachable_space` / native `reachable_space` | BFS flood-fill from a coordinate. |
| A* path | `battlesnake.core.astar` | Useful for food distance and route checks. |
| Voronoi territory | `battlesnake.core.voronoi` / native `voronoi_territory` | Live native function returning per-snake controlled cells. First-class v1 signal. |
| Choke points | `battlesnake.core.choke_points` | Useful for trap risk and corridor pressure. |
| Edge trap move | `battlesnake.core.edge_trap` | Existing trap-related primitive. |
| Hazard prediction | `battlesnake.core.hazard` | Mostly relevant for Royale, but available. |
| Board evaluation | `battlesnake.core.evaluation.evaluate` / native `evaluate` | Weighted C evaluator already used by search. |

The current native evaluator already includes these weighted terms:

| Term | Current default | Meaning |
|---|---:|---|
| `terminal_win` | `1000000.0` | Utility for being the only remaining snake. |
| `terminal_loss` | `-1000000.0` | Utility for dead/missing snake. |
| `base` | `500.0` | Non-terminal baseline. |
| `health` | `0.7` | Higher health is better. |
| `length` | `18.0` | Longer body is better. |
| `reachable_space` | `4.0` | More flood-fill area is better. |
| `safe_moves` | `35.0` | More immediate escapes are better. |
| `center` | `2.0` | Center preference. |
| `food` | `55.0` | Nearest-food attraction at normal health. |
| `low_health_food` | `120.0` | Nearest-food attraction below health threshold. |
| `low_health_threshold` | `35.0` | Health cutoff for food urgency. |
| `hazard_damage` | `1.0` | Per-turn hazard damage penalty multiplier. |
| `hazard` | `25.0` | Fixed hazard occupancy penalty. |
| `length_advantage` | `5.0` | Relative length advantage against other snakes. |
| `adjacent_equal_or_longer_penalty` | `120.0` | Penalty near equal/longer heads. |
| `adjacent_shorter_bonus` | `45.0` | Bonus near shorter heads. |
| `opponent_reachable_space` | `0.0` | Available but disabled by default. Enabled in v1 FFA scoring. |
| `territory_delta` | `0.0` | Available but disabled by default. Enabled in v1 FFA scoring. |
| `opponent_safe_moves` | `0.0` | Available but disabled by default. Enabled in v1 FFA scoring. |
| `opponent_low_health_food_denial` | `0.0` | Available but disabled by default. |

### Already Available From Opponent Model Work

The Standard FFA opponent model can score:

```text
P(opponent_move | board, opponent_snake, candidate_move)
```

for four candidate moves per opponent snake. The current model should be used
as an opponent prior, not as our move selector.

Existing model features include:

| Feature family | Examples |
|---|---|
| Board phase | turn, board width, board height, alive snake count. |
| Snake state | health, length, rank. |
| Candidate safety | safe move count, candidate is safe, in bounds, occupied without tails. |
| Food/hazard | candidate is food, candidate is hazard, distance to nearest food. |
| Space | candidate reachable space. |
| Head pressure | adjacent longer/equal heads, adjacent shorter heads. |

This overlaps with useful Standard gameplay metrics and gives us a clear
interface for future C scoring:

```text
OpponentMovePrior(board, opponent_id) -> {
    up: p_up,
    down: p_down,
    left: p_left,
    right: p_right
}
```

### Model Sequencing Decision

The learned prior is deliberately **not** part of v1:

1. v1 ships with the deterministic uniform-safe prior plus forced danger
   injection (see Opponent Priors).
2. The LightGBM prior is added later as a drop-in swap on the dev snake, and
   must win an arena A/B on the placement objective — not on offline move
   accuracy — without raising death rates, to stay enabled.
3. The C inference path for the model (issue #14) is justified only by that
   arena verdict. Until then, no production plan depends on the model.

Expected outcome to falsify experimentally: at scenario depth 1, the learned
prior mostly reorders scenarios that uniform-safe plus danger injection already
cover; its real value grows with search depth, where move ordering compounds.

## Missing Signals We Need For Strong Standard Play

These are strategy concepts we need, even if their implementation can reuse
existing primitives:

| Signal | Definition | Existing support | Notes |
|---|---|---|---|
| Candidate death class | Why a candidate move dies: wall, body, self, head-to-head, hazard starvation, trapped next turn. | Partly available | Needs explicit classification for debugging and tuning. |
| Post-move reachable space | Reachable cells after our candidate move and each sampled opponent response. | Available via `clone_and_apply` + `reachable_space` | Should be cached per root candidate/scenario. |
| Escape count after response | Safe moves available one turn after scenario simulation. | Available via `safe_moves` | Important against traps. |
| Contested cell probability | Probability that an opponent also moves into our candidate head cell. | Needs opponent prior | Core model use case. |
| Losing head-to-head probability | Probability mass from equal/longer opponents entering our candidate cell or adjacent attack cells. | Partly available | Should be hard or near-hard penalty. |
| Food race margin | Difference between our distance to food and each opponent's distance/probable route. | Available via `voronoi_territory` race distance | v1 uses deterministic Voronoi race distance, not the model. |
| Tail release estimate | Whether an occupied tail cell is likely to vacate. | Partly available | Needs careful handling when snake eats. |
| Choke/trap risk | Whether candidate enters a corridor/pocket controlled by opponent heads. | Partly available via choke points/reachable space | Needs scenario-level scoring. |
| Territory share | Approximate cells controlled by each snake under simultaneous expansion. | Available via native `voronoi_territory` and evaluator `territory_delta` | First-class v1 term. |
| Opponent kill opportunity | Probability our move causes or enables an opponent death without exposing us. | Needs scenario simulation | Reward should be clipped to avoid suicidal aggression. |

## Move Evaluation

### Candidate Set

Start with all four moves. For each move `m`, compute:

```text
next_head(m) = step(our_head, m)
```

Then classify it:

```text
ImmediateCandidate(m) = {
    in_bounds,
    safe_by_board_rules,
    enters_hazard,
    candidate_food,
    candidate_occupied,
    immediate_safe_move,
    immediate_reachable_space,
    immediate_safe_moves_after_move
}
```

Unsafe moves are not discarded completely until all moves are scored; they are
assigned terminal-class penalties. This lets the fallback pick the least-bad
move when every option is bad.

### Opponent Priors

For each alive opponent `o`, produce a move distribution:

```text
P_o = OpponentMovePrior(board, o)
```

The v1 prior is deterministic:

1. Assign probability `0` to moves that are immediately impossible for `o`.
2. Assign uniform probability over safe moves.
3. If `o` has no safe moves, assign uniform over all four moves only for
   bookkeeping; the scenario simulator will kill that snake.

When the learned model prior is enabled (see Model Sequencing Decision), it
must be calibrated before use:

```text
P'_o(a) = (1 - epsilon) * P_o(a) + epsilon * UniformSafe_o(a)
```

Initial `epsilon` should be non-zero (starting range `0.10-0.15`). This
prevents the strategy from treating a low-probability lethal move as
impossible. Danger injection (below) stays active regardless of which prior is
in use — the prior never removes a forced worst-case scenario.

### Joint Response Sampling

For `N` opponents, full enumeration is `4^N`. Standard FFA should not rely on
full enumeration as the primary strategy.

Instead, build a small scenario set:

```text
J = TopKJointResponses(P'_1, P'_2, ..., P'_N)
```

The default scenario set should include:

1. Highest-probability joint responses by beam search.
2. Any response where an equal/longer opponent can collide with our candidate
   head.
3. Any response where an opponent can take the same food we are targeting.
4. A conservative worst-case response for each nearby opponent.

This means the prior orders and weights likely responses, but tactical dangers
are injected even if their prior probability is low.

#### Relevance-Pruned Deepening

The primary path to seeing multi-turn traps is **opponent relevance pruning**,
not wider joint enumeration. Within a horizon of `d` turns, only opponents
whose head is within roughly `2d` Manhattan distance can interact with us.
Distant snakes are frozen as static obstacles (bodies block, heads do not
move).

In four-snake games this leaves 0-2 relevant opponents, which makes depth 2-3
search cheaper than wide depth-1 joint sampling over all opponents — and
strictly better at trap detection:

- 0 relevant opponents: solo space search (corridor/pocket detection).
- 1 relevant opponent: reuse the duel search machinery where ruleset semantics
  allow.
- 2 relevant opponents: shallow simultaneous expansion over the pruned pair.

Deepening runs only for top root candidates under the remaining time budget,
and a timed-out deeper result is discarded in favor of the completed depth-1
result — never exposed partially.

### Scenario Utility

For our candidate move `m` and joint opponent response `j`, simulate:

```text
board' = board.clone_and_apply({
    our_id: m,
    opponent_1: j_1,
    ...
    opponent_N: j_N
})
```

Then compute:

```text
U(m, j) =
    terminal_utility(board')
  + board_eval(board', our_id)
  + standard_adjustments(board, board', our_id, m, j)
```

`board_eval` should start from the existing native evaluator with the FFA
terms enabled (`territory_delta`, `opponent_safe_moves`,
`opponent_reachable_space`). The Standard strategy should add FFA-specific
adjustments instead of replacing the evaluator.

### Standard Adjustments

The adjustment layer should contain these metric families:

#### Space and Escape

```text
space_score =
    w_space_log * log1p(reachable_space_after)
  + w_space_ratio * reachable_space_after / max(our_length, 1)
  + w_escape * safe_moves_after
  - w_zero_escape * I[safe_moves_after == 0]
```

Rationale:

- Raw reachable space matters.
- Space relative to length matters more than raw cells in mid/late game.
- Zero escape next turn is a severe warning even when current move is legal.

#### Head Pressure

```text
head_score =
  - w_losing_h2h * P[equal_or_longer_head_collision]
  + w_winning_h2h * P[shorter_head_collision]
  - w_adjacent_danger * adjacent_equal_or_longer_heads_after
```

The losing head-to-head term should be close to a hard constraint. The winning
head-to-head term must be clipped because chasing kills is often worse than
preserving space.

#### Food and Hunger

```text
hunger = clamp((food_urgency_health - health_after) / food_urgency_health, 0, 1)

food_score =
    hunger * w_food_route / (route_distance_to_food + 1)
  + w_food_on_cell * I[next_head_is_food]
  - w_food_contested * I[opponent_wins_or_ties_voronoi_race_to_target_food]
```

Food priority should grow smoothly as health decreases. When health is high,
food should be mostly a positional bonus. When health is low, food becomes a
survival requirement.

Contested-food detection in v1 is deterministic: a food cell is ours when we
win the Voronoi race distance to it strictly. Take contested food only when we
are strictly closer and an escape exists after eating; refuse ties unless we
are longer than the contester. The model-based
`P[opponent_reaches_target_food_first_or_same_turn]` refinement is a follow-up
once the learned prior has earned its place.

#### Hazard

```text
hazard_score =
  - w_hazard_entry * I[next_head_is_hazard]
  - w_hazard_damage * expected_hazard_damage
  - w_hazard_starvation * I[health_after <= forced_hazard_exit_distance]
```

Standard boards normally do not have hazards, but the strategy should keep the
metric because the shared code handles hazard data and some rulesets may still
pass it through.

#### Territory and Chokes

```text
territory_score =
    w_territory_delta * (our_controlled_cells - max_opponent_controlled_cells)
  - w_choke_entry * I[entering_choke_controlled_by_equal_or_longer_head]
  - w_pocket * I[reachable_space_after < pocket_space_threshold]
```

This is the main anti-trap layer and a first-class v1 term: territory delta
subsumes space preservation, trap avoidance, and food-race pressure in one
number, and the native `voronoi_territory` plus the evaluator `territory_delta`
term already implement it. It should still be conservative at first: reward
territory mildly, penalize bad pockets strongly.

#### Opponent Death and Placement

```text
placement_score =
    w_opponent_death * expected_opponents_dead_after
  + w_outlive_short_term * P[at_least_one_opponent_dies_soon]
  - w_suicide_trade * P[we_die_in_same_scenario]
```

In Standard FFA, making one opponent die can improve placement. But suicidal
trades should be bad unless the game is already unwinnable and all alternatives
are worse.

## Candidate Aggregation Formula

For each candidate move `m`, aggregate scenario utilities using expected value
and worst case:

```text
Expected(m) = sum_j P(j) * U(m, j)
Worst(m) = min_j U(m, j)

StandardScore(m) =
    hard_gate_penalty(m)
  + w_expected * Expected(m)
  + w_worst * Worst(m)
  + w_immediate * ImmediateUtility(m)
  + tie_breakers(m)
```

Initial behavior should be risk-aware:

```text
w_expected > 0
w_worst > 0
w_worst < w_expected in normal positions
w_worst increases in cramped or low-health positions
```

This gives the snake a pragmatic FFA style:

- It does not act as if every opponent always chooses the worst possible move.
- It does not ignore low-probability lethal moves.
- It can take controlled risks when the expected placement benefit is high.

**CVaR is a documented follow-up, not a v1 requirement.** Two aggregation
weights plus deterministic danger injection keep the v1 tuning surface small.
Add a CVaR term only if arena results show mis-calibrated risk behavior — the
snake being consistently too timid or too reckless against mid-probability
danger — that expected/worst weighting cannot fix.

## Hard Gates

Hard gates are not normal tunable weights. They are rule constraints or
near-rule constraints.

| Gate | Effect |
|---|---|
| Wall collision | Terminal loss. |
| Body/self collision | Terminal loss unless the simulator proves the cell is vacated safely. |
| Equal/longer head-to-head on our target cell | Terminal or near-terminal penalty, depending on scenario probability. |
| Hazard death | Terminal loss if health cannot survive hazard damage. |
| No reachable space | Severe penalty even if current move is legal. |
| No safe next move | Severe penalty, but not terminal if all alternatives are worse. |
| Model-only safety claim | Never accepted. Model cannot make unsafe moves safe. |

## Game Modes

The same scoring formula should adapt through state-dependent multipliers.

Two-snake endgames are not a mode of this strategy: routing already switches
to duel minimax when `snake_count == 2`, both in the Python server
(`select_strategy`) and in the native server, including mid-game when a
four-snake match narrows to two. This strategy only ever sees three or more
alive snakes.

### Normal Mode

Condition:

```text
health >= 45
reachable_space_after is adequate
no immediate equal/longer head threat
```

Behavior:

- Prefer space, escape count, and centrality.
- Take food if cheap.
- Avoid narrow pockets.
- Use opponent prior mostly for contested-cell avoidance and move ordering.

### Hungry Mode

Condition:

```text
health < 35
```

Behavior:

- Increase food route weight.
- Increase willingness to enter moderate risk for food.
- Still reject likely head-to-head death.
- Prefer food routes with escape after eating.

### Cramped Mode

Condition:

```text
reachable_space_after / length is low
or safe_moves_after <= 1
```

Behavior:

- Increase worst-case weight.
- Strongly prefer moves that open escape count.
- Penalize choke entry and opponent-controlled pockets.
- Reduce aggression bonuses.

### Advantage Mode

Condition:

```text
we are longer than nearby opponents
and have adequate reachable space
```

Behavior:

- Allow controlled head pressure.
- Reward taking central contested cells against shorter snakes.
- Still cap kill bonuses to avoid throwing placement.

## What We Should Optimize

The tuning target is gameplay quality, not just one-turn move prediction.

Primary objective:

```text
Objective = E[placement_score] + survival_bonus - avoidable_death_penalty
```

Recommended concrete objective:

```text
placement_score =
    1.00 * P[first place]
  + 0.55 * P[second place]
  + 0.20 * P[third place]
  + 0.00 * P[fourth place]

turn_score =
    survival_turns / max_game_turns

death_penalty =
    frequency-weighted penalty by death cause
    (wall, body, head-to-head, starvation), as reported by the simulator

Objective =
    w_place * placement_score
  + w_turns * turn_score
  - w_death * death_penalty
  - w_latency * timeout_or_slow_turn_rate
```

Death causes come from simulator/replay classification. Labeling deaths as
"avoidable" is deliberately deferred: cause frequency is a good enough proxy
for v1 tuning and far cheaper than defining avoidability.

The exact coefficients are tunable, but the ranking of concerns is not:
timeouts and deaths are more damaging than small placement gains.

## Weight Optimization Method

The strategy should expose a single weight vector:

```text
theta = {
    expected_weight,
    worst_weight,
    space_log,
    space_ratio,
    escape,
    zero_escape,
    losing_h2h,
    winning_h2h,
    food_route,
    food_contested,
    hazard_entry,
    territory_delta,
    choke_entry,
    pocket,
    opponent_death,
    suicide_trade,
    prior_smoothing,
    joint_top_k,
    danger_injection_threshold
}
```

Hard gates should stay outside `theta`. A `cvar_weight` joins `theta` only if
the CVaR follow-up is adopted.

Optimization should proceed in layers:

1. **Hand-set conservative seed.**
   Start with high survival penalties, moderate space rewards, low aggression
   rewards, and non-zero opponent-prior smoothing.

2. **Scenario-suite tuning.**
   Maintain a curated suite of Standard FFA positions with expected qualitative
   behavior: avoid wall, take only food, avoid contested food, escape pocket,
   pressure shorter snake, reject suicidal kill.

3. **Replay counterfactual scoring.**
   For replay positions, compare chosen move against eventual outcome labels
   where available. This is noisy because replay opponents are not controlled,
   but it is useful for detecting obvious bad preferences.

4. **Self-play or arena evaluation.**
   Run paired batches against fixed baselines. Compare versions using identical
   game seeds/opponent sets when possible.

5. **Black-box weight search.**
   Use Optuna/CMA-ES/random search on the compute node for soft weights only.
   Optimize the gameplay objective above, with latency as a constraint.

6. **Ablation checks.**
   Every candidate weight set should be compared against:
   - no opponent prior;
   - uniform safe-move opponent prior;
   - LightGBM opponent prior;
   - no scenario simulation, immediate evaluator only.

This prevents us from overfitting to the learned model or to a single benchmark
population.

## Model Prior Usage

The model prior should affect these parts of the strategy:

| Use | Allowed? | Reason |
|---|---|---|
| Ordering opponent responses | Yes | Reduces combinatorial branching. |
| Weighting expected scenario utility | Yes | Main value of the model. |
| Estimating contested cell probability | Yes | Directly relevant to FFA safety. |
| Estimating food race pressure | Yes | Useful but must be checked with distance/space. |
| Declaring a move safe | No | Safety is deterministic/rules-based. |
| Ignoring low-probability lethal moves | No | Opponent model is not a proof. |
| Choosing our move directly | No | The model predicts opponents, not our utility. |

See Model Sequencing Decision for when the model prior enters the strategy at
all.

## Latency Budget

The first Standard strategy should leave room for HTTP parsing and response
serialization. Suggested target:

```text
StrategyStandard.decide p95 <= 80 ms locally
StrategyStandard.decide p95 <= 120 ms on deployment hardware
hard timeout fallback before game timeout - safety_margin
```

Root-only model usage is acceptable. Model calls inside deep node expansion are
not acceptable until the C-core model path exists and is measured.

Recommended root limits:

```text
opponents_scored <= 3
candidate_our_moves <= 4
joint_response_scenarios <= 12 initially
scenario_depth = 1 initially
relevance-pruned extension depth = 2-3 only for top moves and under budget
```

## Initial Default Behavior

Before weight optimization, the strategy should be intentionally conservative:

1. Filter immediate losing moves.
2. Prefer moves with larger post-move reachable space and territory delta.
3. Penalize moves with zero or one next-turn escape.
4. Increase food weight below health `35`.
5. Penalize equal/longer head threats heavily.
6. Use the uniform-safe opponent prior to rank likely contested cells.
7. Use expected utility only after hard and near-hard tactical risks are
   accounted for.

Expected style:

- It should survive longer than first-safe fallback.
- It should not chase kills blindly.
- It should take food when hungry.
- It should avoid small pockets even when they contain food.
- It should become more adversarial only when space and length advantage make
  that reasonable.

## Validation Metrics

Track these metrics per game and per turn:

| Metric | Purpose |
|---|---|
| Placement | Primary FFA outcome. |
| Survival turns | Secondary outcome. |
| Death cause | Detect avoidable tactical failures. |
| Move legality fallback rate | Should be near zero after hard gates. |
| Immediate safe move count chosen | Detect preference for low-escape moves. |
| Reachable space after chosen move | Detect pocket bias. |
| Food taken when health low | Detect starvation behavior. |
| Contested-cell entry rate | Detect over/under-risk. |
| Equal/longer head collision deaths | Must trend down. |
| Opponent prior entropy | Detect model overconfidence/underconfidence. |
| Scenario count per turn | Keep latency bounded. |
| Strategy latency p50/p95/p99 | Production safety. |

## Success Criteria

The Standard gameplay strategy is good enough to keep iterating when it meets
these criteria:

1. It materially outperforms first-safe fallback on Standard FFA replay/arena
   tests.
2. It has fewer avoidable immediate deaths than first-safe fallback.
3. LightGBM prior improves at least one gameplay metric over uniform safe-move
   prior without increasing avoidable death rate.
4. p95 decision latency stays inside the budget.
5. Failure cases are inspectable through recorded per-move metric breakdowns.

## Open Strategy Questions

These are strategic questions to answer with experiments, not assumptions:

1. How much probability mass should be reserved for low-probability dangerous
   opponent moves? (Starting point: epsilon `0.10-0.15`; danger injection
   matters more than the exact epsilon.)
2. Does the current opponent model improve placement, or only make move
   predictions look better offline? (See Model Sequencing Decision — answered
   by arena A/B, not offline metrics.)
3. How often should the strategy deliberately take contested food when hungry?
   (v1 rule: strictly closer by Voronoi race and escape after eating; refuse
   ties unless longer. Tune from there.)
4. What interaction radius makes relevance pruning safe — how often would a
   frozen snake have interacted within the search horizon?

Resolved since the first draft of this spec:

- **CVaR necessity** — deferred behind Expected+Worst plus danger injection;
  revisit only on arena evidence of mis-calibrated risk.
- **Territory metric choice** — native `voronoi_territory` plus the evaluator
  `territory_delta` term; root-only.
- **Endgame handoff point** — already handled by existing 2-snake routing to
  duel minimax in both servers.
