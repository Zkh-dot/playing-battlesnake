#include "standard_ffa.h"

#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define STANDARD_MAX_SNAKES 8
#define STANDARD_MAX_SCENARIOS 16
#define STANDARD_TERMINAL_LOSS -1000000.0

typedef struct {
    MoveDirection move;
    Coord target;
    bool in_bounds;
    bool safe_by_rules;
    bool food;
    bool hazard;
    int immediate_safe_count;
    int immediate_space;
    int severity;
    bool terminal;
    bool severe;
    bool eligible;
    double score;
    double expected;
    double worst;
    bool refused_trap;
} StandardCandidate;

typedef struct {
    const char* ids[STANDARD_MAX_SNAKES];
    MoveDirection moves[STANDARD_MAX_SNAKES];
    int count;
    double probability;
} StandardScenario;

static int standard_snake_length(const Snake* snake) {
    return snake->length > 0 ? snake->length : snake->body_len;
}

static int standard_manhattan(Coord left, Coord right) {
    return abs(left.x - right.x) + abs(left.y - right.y);
}

static bool standard_coord_in_array(const Coord* coords, int count, Coord coord) {
    for (int i = 0; i < count; i++) {
        if (CoordEquals(coords[i], coord)) {
            return true;
        }
    }
    return false;
}

static bool standard_coord_in_snake_body(const Snake* snake, Coord coord) {
    if (snake == NULL) {
        return false;
    }
    for (int i = 0; i < snake->body_len; i++) {
        if (CoordEquals(snake->body[i], coord)) {
            return true;
        }
    }
    return false;
}

static bool standard_is_constrictor(const Board* board) {
    return board->ruleset_name != NULL && strcmp(board->ruleset_name, "constrictor") == 0;
}

CoreStandardFfaConfig CoreStandardFfaConfigDefault(int time_budget_ms) {
    CoreStandardFfaConfig config;
    config.evaluation = CoreEvaluationWeightsDefault();
    config.evaluation.terminal_win = 1000000.0;
    config.evaluation.terminal_loss = -1000000.0;
    config.evaluation.base = 500.0;
    config.evaluation.health = 0.7;
    config.evaluation.length = 18.0;
    config.evaluation.reachable_space = 4.0;
    config.evaluation.safe_moves = 35.0;
    config.evaluation.center = 2.0;
    config.evaluation.food = 55.0;
    config.evaluation.low_health_food = 120.0;
    config.evaluation.low_health_threshold = 35.0;
    config.evaluation.hazard_damage = 1.0;
    config.evaluation.hazard = 25.0;
    config.evaluation.length_advantage = 5.0;
    config.evaluation.adjacent_equal_or_longer_penalty = 120.0;
    config.evaluation.adjacent_shorter_bonus = 45.0;
    config.evaluation.opponent_reachable_space = 0.22527449501678923;
    config.evaluation.territory_delta = 1.5929437053687334;
    config.evaluation.opponent_safe_moves = 26.213057618920736;
    config.evaluation.opponent_low_health_food_denial = 0.0;
    config.w_expected = 0.9689609000660646;
    config.w_worst = 0.37931599622514295;
    config.w_space_log = 77.79952998896476;
    config.w_space_ratio = 16.151019864561057;
    config.w_escape = 73.01729135878779;
    config.w_zero_escape = 787.0676758761093;
    config.w_losing_h2h = 893412.0045994517;
    config.w_winning_h2h = 146.8920317041263;
    config.w_food_on_cell = 299.49531231126576;
    config.w_food_route = 227.82484655743104;
    config.w_contested_food = 537.5724004886563;
    config.w_pocket = 606.598196752109;
    config.food_urgency_health = 26.06690774845463;
    config.pocket_space_per_length = 1.9583885545066404;
    config.nearby_opponent_distance = 2.806691545337101;
    config.deepening_enabled = 1.0;
    config.deepening_depth = 3.0;
    config.deepening_top_candidates = 2.0;
    config.deepening_interaction_radius = 6.0;
    config.deepening_trap_penalty = 900000.0;
    config.max_scenarios = 12;
    config.time_budget_ms = time_budget_ms > 0 ? time_budget_ms : 80;
    return config;
}

static Board* standard_board_after_own_static(const Board* board, const char* snake_id, MoveDirection move) {
    const Snake* snake = BoardFindSnakeConst(board, snake_id);
    if (snake == NULL || snake->body_len == 0) {
        return NULL;
    }

    Coord target = MoveStep(SnakeHead(snake), move);
    bool ate_food = standard_coord_in_array(board->food, board->food_count, target);
    bool hazard = standard_coord_in_array(board->hazards, board->hazard_count, target);
    bool grew = ate_food || standard_is_constrictor(board);
    int health = snake->health - 1 - (hazard ? board->hazard_damage : 0);
    if (ate_food) {
        health = 100;
    }

    Board* next = BoardCreate(board->width, board->height, board->ruleset_name, board->hazard_damage);
    if (next == NULL) {
        return NULL;
    }

    for (int i = 0; i < board->snake_count; i++) {
        const Snake* current = &board->snakes[i];
        if (strcmp(current->id, snake_id) != 0) {
            if (!BoardAddSnake(next, current)) {
                BoardFree(next);
                return NULL;
            }
            continue;
        }

        if (!BoardInBounds(board, target) || health <= 0) {
            continue;
        }

        int body_len = current->body_len + (grew ? 1 : 0);
        Coord* body = (Coord*)malloc((size_t)body_len * sizeof(Coord));
        if (body == NULL) {
            BoardFree(next);
            return NULL;
        }
        body[0] = target;
        int copy_count = grew ? current->body_len : current->body_len - 1;
        for (int j = 0; j < copy_count; j++) {
            body[j + 1] = current->body[j];
        }
        Snake moved;
        SnakeInit(&moved, current->id, current->name, health, body, body_len);
        moved.length = body_len;
        free(body);
        bool ok = BoardAddSnake(next, &moved);
        SnakeFree(&moved);
        if (!ok) {
            BoardFree(next);
            return NULL;
        }
    }

    for (int i = 0; i < board->food_count; i++) {
        if (!CoordEquals(board->food[i], target) && !BoardAddFood(next, board->food[i])) {
            BoardFree(next);
            return NULL;
        }
    }
    for (int i = 0; i < board->hazard_count; i++) {
        if (!BoardAddHazard(next, board->hazards[i])) {
            BoardFree(next);
            return NULL;
        }
    }
    return next;
}

static int standard_reachable(const Board* board, Coord start, const char* snake_id) {
    int reachable = 0;
    if (CoreReachableSpace(board, start, snake_id, &reachable) != CORE_OK) {
        return 0;
    }
    return reachable;
}

static void standard_classify_candidates(
    const Board* board,
    const char* snake_id,
    StandardCandidate candidates[4]
) {
    const Snake* snake = BoardFindSnakeConst(board, snake_id);
    Coord head = SnakeHead(snake);
    int own_length = standard_snake_length(snake);

    for (int move = MOVE_UP; move <= MOVE_RIGHT; move++) {
        StandardCandidate* candidate = &candidates[move];
        memset(candidate, 0, sizeof(*candidate));
        candidate->move = (MoveDirection)move;
        candidate->target = MoveStep(head, (MoveDirection)move);
        candidate->in_bounds = BoardInBounds(board, candidate->target);
        candidate->safe_by_rules = BoardIsSafe(board, candidate->target, snake_id);
        candidate->food = standard_coord_in_array(board->food, board->food_count, candidate->target);
        candidate->hazard = standard_coord_in_array(board->hazards, board->hazard_count, candidate->target);
        candidate->score = STANDARD_TERMINAL_LOSS;
        bool hazard_starvation = candidate->hazard && !candidate->food && snake->health <= board->hazard_damage + 1;

        if (candidate->in_bounds && candidate->safe_by_rules && !hazard_starvation) {
            Board* after = standard_board_after_own_static(board, snake_id, (MoveDirection)move);
            if (after != NULL && BoardFindSnakeConst(after, snake_id) != NULL) {
                MoveDirection safe_moves[4];
                candidate->immediate_safe_count = BoardSafeMoves(after, snake_id, safe_moves);
                candidate->immediate_space = standard_reachable(after, SnakeHead(BoardFindSnakeConst(after, snake_id)), snake_id);
            }
            BoardFree(after);
        }

        if (!candidate->in_bounds) {
            candidate->terminal = true;
            candidate->severity = 50;
        } else if (hazard_starvation) {
            candidate->terminal = true;
            candidate->severity = 30;
        } else if (!candidate->safe_by_rules) {
            candidate->terminal = true;
            candidate->severity = 40;
            if (!standard_coord_in_snake_body(snake, candidate->target)) {
                for (int i = 0; i < board->snake_count; i++) {
                    const Snake* other = &board->snakes[i];
                    if (strcmp(other->id, snake_id) == 0 || other->body_len == 0) {
                        continue;
                    }
                    if (standard_snake_length(other) >= own_length) {
                        Coord other_head = SnakeHead(other);
                        for (int other_move = MOVE_UP; other_move <= MOVE_RIGHT; other_move++) {
                            if (CoordEquals(candidate->target, MoveStep(other_head, (MoveDirection)other_move))) {
                                candidate->severity = 20;
                            }
                        }
                    }
                }
            }
        } else if (candidate->immediate_space <= 0 || candidate->immediate_safe_count <= 0) {
            candidate->severe = true;
            candidate->severity = 10;
        }
        candidate->eligible = candidate->safe_by_rules && !candidate->terminal && !candidate->severe;
    }
}

static int standard_least_bad(const StandardCandidate candidates[4]) {
    int best = MOVE_UP;
    for (int move = MOVE_DOWN; move <= MOVE_RIGHT; move++) {
        const StandardCandidate* left = &candidates[best];
        const StandardCandidate* right = &candidates[move];
        if (right->severity < left->severity ||
            (right->severity == left->severity && right->immediate_space > left->immediate_space) ||
            (right->severity == left->severity && right->immediate_space == left->immediate_space &&
             right->immediate_safe_count > left->immediate_safe_count)) {
            best = move;
        }
    }
    return best;
}

static int standard_default_opponent_moves(
    const Board* board,
    const char* snake_id,
    const char* ids[STANDARD_MAX_SNAKES],
    MoveDirection moves[STANDARD_MAX_SNAKES][4],
    int counts[STANDARD_MAX_SNAKES]
) {
    int opponent_count = 0;
    for (int i = 0; i < board->snake_count && opponent_count < STANDARD_MAX_SNAKES; i++) {
        const Snake* snake = &board->snakes[i];
        if (strcmp(snake->id, snake_id) == 0) {
            continue;
        }
        ids[opponent_count] = snake->id;
        counts[opponent_count] = BoardSafeMoves(board, snake->id, moves[opponent_count]);
        if (counts[opponent_count] <= 0) {
            moves[opponent_count][0] = MOVE_UP;
            counts[opponent_count] = 1;
        }
        opponent_count++;
    }
    return opponent_count;
}

static void standard_emit_scenarios(
    const char* ids[STANDARD_MAX_SNAKES],
    MoveDirection moves[STANDARD_MAX_SNAKES][4],
    const int counts[STANDARD_MAX_SNAKES],
    int opponent_count,
    int index,
    StandardScenario* current,
    StandardScenario scenarios[STANDARD_MAX_SCENARIOS],
    int* scenario_count,
    int scenario_limit
) {
    if (*scenario_count >= scenario_limit) {
        return;
    }
    if (index == opponent_count) {
        scenarios[*scenario_count] = *current;
        (*scenario_count)++;
        return;
    }
    current->ids[index] = ids[index];
    current->count = opponent_count;
    for (int i = 0; i < counts[index] && *scenario_count < scenario_limit; i++) {
        current->moves[index] = moves[index][i];
        standard_emit_scenarios(ids, moves, counts, opponent_count, index + 1, current, scenarios, scenario_count, scenario_limit);
    }
}

static int standard_scenarios(
    const Board* board,
    const char* snake_id,
    StandardScenario scenarios[STANDARD_MAX_SCENARIOS],
    int limit
) {
    const char* ids[STANDARD_MAX_SNAKES];
    MoveDirection moves[STANDARD_MAX_SNAKES][4];
    int counts[STANDARD_MAX_SNAKES];
    int opponent_count = standard_default_opponent_moves(board, snake_id, ids, moves, counts);
    StandardScenario current;
    memset(&current, 0, sizeof(current));
    current.probability = 1.0;
    int scenario_count = 0;
    standard_emit_scenarios(ids, moves, counts, opponent_count, 0, &current, scenarios, &scenario_count, limit);
    if (scenario_count == 0) {
        memset(&scenarios[0], 0, sizeof(scenarios[0]));
        scenarios[0].probability = 1.0;
        scenario_count = 1;
    }
    double probability = 1.0 / (double)scenario_count;
    for (int i = 0; i < scenario_count; i++) {
        scenarios[i].probability = probability;
    }
    return scenario_count;
}

static Board* standard_apply_scenario(
    const Board* board,
    const char* snake_id,
    MoveDirection own_move,
    const StandardScenario* scenario
) {
    const char* ids[STANDARD_MAX_SNAKES + 1];
    MoveDirection moves[STANDARD_MAX_SNAKES + 1];
    int count = 0;
    ids[count] = snake_id;
    moves[count++] = own_move;
    for (int i = 0; i < scenario->count && count < STANDARD_MAX_SNAKES + 1; i++) {
        ids[count] = scenario->ids[i];
        moves[count++] = scenario->moves[i];
    }
    return BoardCloneAndApply(board, ids, moves, count);
}

static double standard_space_adjustment(const Board* board, const char* snake_id, const CoreStandardFfaConfig* config) {
    const Snake* snake = BoardFindSnakeConst(board, snake_id);
    if (snake == NULL || snake->body_len == 0) {
        return STANDARD_TERMINAL_LOSS;
    }
    int space = standard_reachable(board, SnakeHead(snake), snake_id);
    MoveDirection safe_moves[4];
    int safe_count = BoardSafeMoves(board, snake_id, safe_moves);
    int length = standard_snake_length(snake);
    if (length <= 0) {
        length = 1;
    }
    double score = config->w_space_log * log1p((double)space);
    score += config->w_space_ratio * ((double)space / (double)length);
    score += config->w_escape * (double)safe_count;
    if (safe_count == 0) {
        score -= config->w_zero_escape;
    }
    return score;
}

static double standard_pocket_adjustment(const Board* board, const char* snake_id, const CoreStandardFfaConfig* config) {
    const Snake* snake = BoardFindSnakeConst(board, snake_id);
    if (snake == NULL || snake->body_len == 0) {
        return STANDARD_TERMINAL_LOSS;
    }
    int space = standard_reachable(board, SnakeHead(snake), snake_id);
    int length = standard_snake_length(snake);
    if (length <= 0) {
        length = 1;
    }
    double threshold = config->pocket_space_per_length * (double)length;
    if ((double)space >= threshold || threshold <= 0.0) {
        return 0.0;
    }
    return -config->w_pocket * ((threshold - (double)space) / threshold);
}

static double standard_head_pressure(
    const Board* board,
    const char* snake_id,
    const StandardCandidate* candidate,
    const StandardScenario* scenario,
    const CoreStandardFfaConfig* config
) {
    const Snake* snake = BoardFindSnakeConst(board, snake_id);
    int own_length = snake != NULL ? standard_snake_length(snake) : 0;
    double score = 0.0;
    for (int i = 0; i < scenario->count; i++) {
        const Snake* other = BoardFindSnakeConst(board, scenario->ids[i]);
        if (other == NULL || other->body_len == 0) {
            continue;
        }
        if (!CoordEquals(MoveStep(SnakeHead(other), scenario->moves[i]), candidate->target)) {
            continue;
        }
        if (standard_snake_length(other) >= own_length) {
            score -= config->w_losing_h2h;
        } else {
            score += config->w_winning_h2h * 0.5;
        }
    }
    return score;
}

static int standard_path_distance(const Board* board, const char* snake_id, Coord target) {
    const Snake* snake = BoardFindSnakeConst(board, snake_id);
    if (snake == NULL || snake->body_len == 0) {
        return INT_MAX;
    }
    Coord* path = NULL;
    int path_count = 0;
    CoreStatus status = CoreShortestPath(board, SnakeHead(snake), target, snake_id, &path, &path_count);
    if (status != CORE_OK || path_count <= 0) {
        free(path);
        return INT_MAX;
    }
    int distance = path_count - 1;
    free(path);
    return distance;
}

static bool standard_wins_food_race(const Board* board, const char* snake_id, Coord food) {
    int my_distance = standard_path_distance(board, snake_id, food);
    if (my_distance == INT_MAX) {
        return false;
    }
    for (int i = 0; i < board->snake_count; i++) {
        const Snake* other = &board->snakes[i];
        if (strcmp(other->id, snake_id) == 0 || other->body_len == 0) {
            continue;
        }
        int other_distance = standard_path_distance(board, other->id, food);
        if (other_distance != INT_MAX && other_distance <= my_distance) {
            return false;
        }
    }
    return true;
}

static double standard_food_adjustment(
    const Board* board,
    const Board* next,
    const char* snake_id,
    const StandardCandidate* candidate,
    const CoreStandardFfaConfig* config
) {
    if (!candidate->food) {
        return 0.0;
    }
    const Snake* snake = BoardFindSnakeConst(board, snake_id);
    if (snake == NULL) {
        return 0.0;
    }
    double hunger = (config->food_urgency_health - (double)snake->health) / config->food_urgency_health;
    if (hunger < 0.0) {
        hunger = 0.0;
    }
    if (hunger > 1.0) {
        hunger = 1.0;
    }
    int route_distance = standard_manhattan(SnakeHead(snake), candidate->target);
    if (route_distance < 1) {
        route_distance = 1;
    }
    double score = config->w_food_on_cell * (1.0 + hunger);
    score += hunger * config->w_food_route / (double)route_distance;
    const Snake* next_snake = BoardFindSnakeConst(next, snake_id);
    if (next_snake == NULL) {
        return score - config->w_contested_food;
    }
    MoveDirection safe_moves[4];
    if (BoardSafeMoves(next, snake_id, safe_moves) <= 0 || !standard_wins_food_race(board, snake_id, candidate->target)) {
        score -= config->w_contested_food;
    }
    return score;
}

static double standard_scenario_utility(
    const Board* board,
    const char* snake_id,
    const StandardCandidate* candidate,
    const StandardScenario* scenario,
    const CoreStandardFfaConfig* config
) {
    Board* next = standard_apply_scenario(board, snake_id, candidate->move, scenario);
    if (next == NULL) {
        return STANDARD_TERMINAL_LOSS;
    }
    if (BoardFindSnakeConst(next, snake_id) == NULL) {
        BoardFree(next);
        return STANDARD_TERMINAL_LOSS;
    }
    double eval = 0.0;
    if (CoreEvaluateWithWeights(next, snake_id, &config->evaluation, &eval) != CORE_OK) {
        BoardFree(next);
        return STANDARD_TERMINAL_LOSS;
    }
    double utility = eval;
    utility += standard_space_adjustment(next, snake_id, config);
    utility += standard_head_pressure(board, snake_id, candidate, scenario, config);
    utility += standard_food_adjustment(board, next, snake_id, candidate, config);
    utility += standard_pocket_adjustment(next, snake_id, config);
    BoardFree(next);
    return utility;
}

static double standard_score_candidate(
    const Board* board,
    const char* snake_id,
    const StandardCandidate* candidate,
    const CoreStandardFfaConfig* config
) {
    StandardScenario scenarios[STANDARD_MAX_SCENARIOS];
    int limit = config->max_scenarios;
    if (limit <= 0 || limit > STANDARD_MAX_SCENARIOS) {
        limit = STANDARD_MAX_SCENARIOS;
    }
    int scenario_count = standard_scenarios(board, snake_id, scenarios, limit < 8 ? limit : 8);
    double weighted_total = 0.0;
    double probability_total = 0.0;
    double worst = INFINITY;
    for (int i = 0; i < scenario_count; i++) {
        double utility = standard_scenario_utility(board, snake_id, candidate, &scenarios[i], config);
        weighted_total += scenarios[i].probability * utility;
        probability_total += scenarios[i].probability;
        if (utility < worst) {
            worst = utility;
        }
    }
    double expected = probability_total > 0.0 ? weighted_total / probability_total : worst;
    return config->w_expected * expected + config->w_worst * worst;
}

static bool standard_solo_has_escape(
    const Board* board,
    const char* snake_id,
    int depth_remaining
) {
    if (BoardFindSnakeConst(board, snake_id) == NULL) {
        return false;
    }
    if (depth_remaining <= 0) {
        return true;
    }
    MoveDirection moves[4];
    int count = BoardSafeMoves(board, snake_id, moves);
    const Snake* snake = BoardFindSnakeConst(board, snake_id);
    Coord head = SnakeHead(snake);
    int filtered_count = 0;
    for (int i = 0; i < count; i++) {
        Coord target = MoveStep(head, moves[i]);
        bool frozen_body = false;
        for (int snake_index = 0; snake_index < board->snake_count; snake_index++) {
            const Snake* other = &board->snakes[snake_index];
            if (strcmp(other->id, snake_id) == 0) {
                continue;
            }
            for (int body_index = 0; body_index < other->body_len; body_index++) {
                if (CoordEquals(target, other->body[body_index])) {
                    frozen_body = true;
                }
            }
        }
        if (!frozen_body) {
            moves[filtered_count++] = moves[i];
        }
    }
    count = filtered_count;
    if (count <= 0) {
        return false;
    }
    for (int i = 0; i < count; i++) {
        Board* next = standard_board_after_own_static(board, snake_id, moves[i]);
        bool ok = next != NULL && standard_solo_has_escape(next, snake_id, depth_remaining - 1);
        BoardFree(next);
        if (ok) {
            return true;
        }
    }
    return false;
}

static int standard_active_opponent_count(const Board* board, const char* snake_id, const CoreStandardFfaConfig* config) {
    const Snake* snake = BoardFindSnakeConst(board, snake_id);
    if (snake == NULL || snake->body_len == 0) {
        return 0;
    }
    double radius = config->deepening_interaction_radius > 0.0 ?
        config->deepening_interaction_radius :
        2.0 * config->deepening_depth;
    int count = 0;
    Coord head = SnakeHead(snake);
    for (int i = 0; i < board->snake_count; i++) {
        const Snake* other = &board->snakes[i];
        if (strcmp(other->id, snake_id) == 0 || other->body_len == 0) {
            continue;
        }
        if ((double)standard_manhattan(head, SnakeHead(other)) <= radius) {
            count++;
        }
    }
    return count;
}

static void standard_apply_deepening_guardrail(
    const Board* board,
    const char* snake_id,
    const CoreStandardFfaConfig* config,
    StandardCandidate candidates[4]
) {
    if (config->deepening_enabled <= 0.0 || standard_active_opponent_count(board, snake_id, config) > 0) {
        return;
    }
    int depth = (int)config->deepening_depth;
    if (depth < 2) {
        return;
    }
    if (depth > 3) {
        depth = 3;
    }
    int top_count = (int)config->deepening_top_candidates;
    if (top_count <= 0) {
        return;
    }
    for (int selected = 0; selected < top_count; selected++) {
        int best = -1;
        for (int move = MOVE_UP; move <= MOVE_RIGHT; move++) {
            if (!candidates[move].eligible || candidates[move].refused_trap) {
                continue;
            }
            if (best < 0 || candidates[move].score > candidates[best].score) {
                best = move;
            }
        }
        if (best < 0) {
            return;
        }
        Board* next = standard_board_after_own_static(board, snake_id, candidates[best].move);
        bool has_escape = next != NULL && standard_solo_has_escape(next, snake_id, depth - 1);
        BoardFree(next);
        if (!has_escape) {
            candidates[best].score = STANDARD_TERMINAL_LOSS;
            candidates[best].refused_trap = true;
        }
    }
}

CoreStatus CoreStandardFfaMove(
    const Board* board,
    const char* snake_id,
    const CoreStandardFfaConfig* config,
    MoveDirection* out_move
) {
    if (board == NULL || snake_id == NULL || out_move == NULL) {
        return CORE_ERROR;
    }
    const Snake* snake = BoardFindSnakeConst(board, snake_id);
    if (snake == NULL || snake->body_len == 0) {
        return CORE_ERROR;
    }
    if (board->snake_count - 1 > STANDARD_MAX_SNAKES) {
        MoveDirection safe_moves[4];
        int safe_count = BoardSafeMoves(board, snake_id, safe_moves);
        *out_move = safe_count > 0 ? safe_moves[0] : MOVE_UP;
        return CORE_OK;
    }

    CoreStandardFfaConfig effective = config != NULL ? *config : CoreStandardFfaConfigDefault(80);
    StandardCandidate candidates[4];
    standard_classify_candidates(board, snake_id, candidates);

    bool any_eligible = false;
    for (int move = MOVE_UP; move <= MOVE_RIGHT; move++) {
        if (candidates[move].eligible) {
            any_eligible = true;
            candidates[move].score = standard_score_candidate(board, snake_id, &candidates[move], &effective);
        }
    }
    if (!any_eligible) {
        *out_move = (MoveDirection)standard_least_bad(candidates);
        return CORE_OK;
    }

    standard_apply_deepening_guardrail(board, snake_id, &effective, candidates);

    int best = -1;
    for (int move = MOVE_UP; move <= MOVE_RIGHT; move++) {
        if (!candidates[move].eligible) {
            continue;
        }
        if (best < 0 || candidates[move].score > candidates[best].score) {
            best = move;
        }
    }
    if (best < 0) {
        *out_move = (MoveDirection)standard_least_bad(candidates);
    } else {
        *out_move = (MoveDirection)best;
    }
    return CORE_OK;
}
