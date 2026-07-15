#include "py_core.h"

#include "../core/core_algorithms.h"
#include "../core/duel_weight_profiles_generated.h"
#include "../core/standard_ffa.h"
#include "../core/zobrist.h"
#include "../py-datatypes/py_datatypes.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static Board* board_from_pyobject(PyObject* object) {
    if (!PyObject_TypeCheck(object, &PyBoardType)) {
        PyErr_SetString(PyExc_TypeError, "board must be a battlesnake_native.Board");
        return NULL;
    }
    PyBoard* py_board = (PyBoard*)object;
    if (py_board->board == NULL) {
        PyErr_SetString(PyExc_ValueError, "board is not initialized");
        return NULL;
    }
    return py_board->board;
}

static PyObject* raise_for_status(CoreStatus status) {
    if (status == CORE_NOT_IMPLEMENTED) {
        PyErr_SetString(PyExc_NotImplementedError, CoreNotImplementedMessage());
        return NULL;
    }
    PyErr_SetString(PyExc_RuntimeError, "core algorithm failed");
    return NULL;
}

static int dict_set_u64(PyObject* dict, const char* key, uint64_t value) {
    PyObject* object = PyLong_FromUnsignedLongLong((unsigned long long)value);
    if (object == NULL) {
        return -1;
    }
    int result = PyDict_SetItemString(dict, key, object);
    Py_DECREF(object);
    return result;
}

static int dict_set_int(PyObject* dict, const char* key, int value) {
    PyObject* object = PyLong_FromLong(value);
    if (object == NULL) {
        return -1;
    }
    int result = PyDict_SetItemString(dict, key, object);
    Py_DECREF(object);
    return result;
}

static int dict_set_double(PyObject* dict, const char* key, double value) {
    PyObject* object = PyFloat_FromDouble(value);
    if (object == NULL) {
        return -1;
    }
    int result = PyDict_SetItemString(dict, key, object);
    Py_DECREF(object);
    return result;
}

static int dict_set_bool(PyObject* dict, const char* key, bool value) {
    PyObject* object = PyBool_FromLong(value ? 1 : 0);
    if (object == NULL) {
        return -1;
    }
    int result = PyDict_SetItemString(dict, key, object);
    Py_DECREF(object);
    return result;
}

static int dict_set_string(PyObject* dict, const char* key, const char* value) {
    PyObject* object = PyUnicode_FromString(value);
    if (object == NULL) {
        return -1;
    }
    int result = PyDict_SetItemString(dict, key, object);
    Py_DECREF(object);
    return result;
}

static int dict_set_none(PyObject* dict, const char* key) {
    return PyDict_SetItemString(dict, key, Py_None);
}

static int parse_optional_weight(
    PyObject* weights_obj,
    const char* key,
    double* value
) {
    PyObject* object = PyDict_GetItemString(weights_obj, key);
    if (object == NULL) {
        return 0;
    }

    double parsed = PyFloat_AsDouble(object);
    if (PyErr_Occurred()) {
        PyErr_Format(PyExc_TypeError, "weights[%s] must be a number", key);
        return -1;
    }
    *value = parsed;
    return 0;
}

static bool is_known_evaluation_weight(const char* key) {
    return strcmp(key, "terminal_win") == 0 ||
        strcmp(key, "terminal_loss") == 0 ||
        strcmp(key, "base") == 0 ||
        strcmp(key, "health") == 0 ||
        strcmp(key, "length") == 0 ||
        strcmp(key, "reachable_space") == 0 ||
        strcmp(key, "safe_moves") == 0 ||
        strcmp(key, "center") == 0 ||
        strcmp(key, "food") == 0 ||
        strcmp(key, "low_health_food") == 0 ||
        strcmp(key, "low_health_threshold") == 0 ||
        strcmp(key, "hazard_damage") == 0 ||
        strcmp(key, "hazard") == 0 ||
        strcmp(key, "length_advantage") == 0 ||
        strcmp(key, "adjacent_equal_or_longer_penalty") == 0 ||
        strcmp(key, "adjacent_shorter_bonus") == 0 ||
        strcmp(key, "opponent_reachable_space") == 0 ||
        strcmp(key, "territory_delta") == 0 ||
        strcmp(key, "opponent_safe_moves") == 0 ||
        strcmp(key, "opponent_low_health_food_denial") == 0;
}

static int parse_evaluation_weights(PyObject* weights_obj, CoreEvaluationWeights* weights) {
    *weights = CoreEvaluationWeightsDefault();
    if (weights_obj == NULL || weights_obj == Py_None) {
        return 0;
    }
    if (!PyDict_Check(weights_obj)) {
        PyErr_SetString(PyExc_TypeError, "weights must be a dict");
        return -1;
    }

    PyObject* key = NULL;
    PyObject* value = NULL;
    Py_ssize_t pos = 0;
    while (PyDict_Next(weights_obj, &pos, &key, &value)) {
        if (!PyUnicode_Check(key)) {
            PyErr_SetString(PyExc_TypeError, "weights keys must be strings");
            return -1;
        }
        const char* key_str = PyUnicode_AsUTF8(key);
        if (key_str == NULL) {
            return -1;
        }
        if (!is_known_evaluation_weight(key_str)) {
            PyErr_Format(PyExc_KeyError, "unknown evaluation weight: %s", key_str);
            return -1;
        }
    }

    if (parse_optional_weight(weights_obj, "terminal_win", &weights->terminal_win) < 0 ||
        parse_optional_weight(weights_obj, "terminal_loss", &weights->terminal_loss) < 0 ||
        parse_optional_weight(weights_obj, "base", &weights->base) < 0 ||
        parse_optional_weight(weights_obj, "health", &weights->health) < 0 ||
        parse_optional_weight(weights_obj, "length", &weights->length) < 0 ||
        parse_optional_weight(weights_obj, "reachable_space", &weights->reachable_space) < 0 ||
        parse_optional_weight(weights_obj, "safe_moves", &weights->safe_moves) < 0 ||
        parse_optional_weight(weights_obj, "center", &weights->center) < 0 ||
        parse_optional_weight(weights_obj, "food", &weights->food) < 0 ||
        parse_optional_weight(weights_obj, "low_health_food", &weights->low_health_food) < 0 ||
        parse_optional_weight(weights_obj, "low_health_threshold", &weights->low_health_threshold) < 0 ||
        parse_optional_weight(weights_obj, "hazard_damage", &weights->hazard_damage) < 0 ||
        parse_optional_weight(weights_obj, "hazard", &weights->hazard) < 0 ||
        parse_optional_weight(weights_obj, "length_advantage", &weights->length_advantage) < 0 ||
        parse_optional_weight(
            weights_obj,
            "adjacent_equal_or_longer_penalty",
            &weights->adjacent_equal_or_longer_penalty
        ) < 0 ||
        parse_optional_weight(weights_obj, "adjacent_shorter_bonus", &weights->adjacent_shorter_bonus) < 0 ||
        parse_optional_weight(weights_obj, "opponent_reachable_space", &weights->opponent_reachable_space) < 0 ||
        parse_optional_weight(weights_obj, "territory_delta", &weights->territory_delta) < 0 ||
        parse_optional_weight(weights_obj, "opponent_safe_moves", &weights->opponent_safe_moves) < 0 ||
        parse_optional_weight(
            weights_obj,
            "opponent_low_health_food_denial",
            &weights->opponent_low_health_food_denial
        ) < 0) {
        return -1;
    }

    return 0;
}

static int parse_standard_ffa_config(PyObject* theta_obj, CoreStandardFfaConfig* config) {
    if (theta_obj == NULL || theta_obj == Py_None) {
        return 0;
    }
    if (!PyDict_Check(theta_obj)) {
        PyErr_SetString(PyExc_TypeError, "theta must be a dict");
        return -1;
    }

    if (parse_optional_weight(theta_obj, "terminal_win", &config->evaluation.terminal_win) < 0 ||
        parse_optional_weight(theta_obj, "terminal_loss", &config->evaluation.terminal_loss) < 0 ||
        parse_optional_weight(theta_obj, "base", &config->evaluation.base) < 0 ||
        parse_optional_weight(theta_obj, "health", &config->evaluation.health) < 0 ||
        parse_optional_weight(theta_obj, "length", &config->evaluation.length) < 0 ||
        parse_optional_weight(theta_obj, "reachable_space", &config->evaluation.reachable_space) < 0 ||
        parse_optional_weight(theta_obj, "safe_moves", &config->evaluation.safe_moves) < 0 ||
        parse_optional_weight(theta_obj, "center", &config->evaluation.center) < 0 ||
        parse_optional_weight(theta_obj, "food", &config->evaluation.food) < 0 ||
        parse_optional_weight(theta_obj, "low_health_food", &config->evaluation.low_health_food) < 0 ||
        parse_optional_weight(theta_obj, "low_health_threshold", &config->evaluation.low_health_threshold) < 0 ||
        parse_optional_weight(theta_obj, "hazard_damage", &config->evaluation.hazard_damage) < 0 ||
        parse_optional_weight(theta_obj, "hazard", &config->evaluation.hazard) < 0 ||
        parse_optional_weight(theta_obj, "length_advantage", &config->evaluation.length_advantage) < 0 ||
        parse_optional_weight(theta_obj, "adjacent_equal_or_longer_penalty", &config->evaluation.adjacent_equal_or_longer_penalty) < 0 ||
        parse_optional_weight(theta_obj, "adjacent_shorter_bonus", &config->evaluation.adjacent_shorter_bonus) < 0 ||
        parse_optional_weight(theta_obj, "opponent_reachable_space", &config->evaluation.opponent_reachable_space) < 0 ||
        parse_optional_weight(theta_obj, "territory_delta", &config->evaluation.territory_delta) < 0 ||
        parse_optional_weight(theta_obj, "opponent_safe_moves", &config->evaluation.opponent_safe_moves) < 0 ||
        parse_optional_weight(theta_obj, "opponent_low_health_food_denial", &config->evaluation.opponent_low_health_food_denial) < 0 ||
        parse_optional_weight(theta_obj, "w_expected", &config->w_expected) < 0 ||
        parse_optional_weight(theta_obj, "w_worst", &config->w_worst) < 0 ||
        parse_optional_weight(theta_obj, "w_space_log", &config->w_space_log) < 0 ||
        parse_optional_weight(theta_obj, "w_space_ratio", &config->w_space_ratio) < 0 ||
        parse_optional_weight(theta_obj, "w_escape", &config->w_escape) < 0 ||
        parse_optional_weight(theta_obj, "w_zero_escape", &config->w_zero_escape) < 0 ||
        parse_optional_weight(theta_obj, "w_losing_h2h", &config->w_losing_h2h) < 0 ||
        parse_optional_weight(theta_obj, "w_winning_h2h", &config->w_winning_h2h) < 0 ||
        parse_optional_weight(theta_obj, "w_food_on_cell", &config->w_food_on_cell) < 0 ||
        parse_optional_weight(theta_obj, "w_food_route", &config->w_food_route) < 0 ||
        parse_optional_weight(theta_obj, "w_contested_food", &config->w_contested_food) < 0 ||
        parse_optional_weight(theta_obj, "w_pocket", &config->w_pocket) < 0 ||
        parse_optional_weight(theta_obj, "food_urgency_health", &config->food_urgency_health) < 0 ||
        parse_optional_weight(theta_obj, "pocket_space_per_length", &config->pocket_space_per_length) < 0 ||
        parse_optional_weight(theta_obj, "nearby_opponent_distance", &config->nearby_opponent_distance) < 0 ||
        parse_optional_weight(theta_obj, "deepening_enabled", &config->deepening_enabled) < 0 ||
        parse_optional_weight(theta_obj, "deepening_depth", &config->deepening_depth) < 0 ||
        parse_optional_weight(theta_obj, "deepening_top_candidates", &config->deepening_top_candidates) < 0 ||
        parse_optional_weight(theta_obj, "deepening_interaction_radius", &config->deepening_interaction_radius) < 0 ||
        parse_optional_weight(theta_obj, "deepening_trap_penalty", &config->deepening_trap_penalty) < 0) {
        return -1;
    }
    return 0;
}

static PyObject* coord_array_to_set_and_free(Coord* coords, int count) {
    PyObject* result = PySet_New(NULL);
    if (result == NULL) {
        free(coords);
        return NULL;
    }

    for (int i = 0; i < count; i++) {
        PyObject* coord = PyCoord_FromCoord(coords[i]);
        if (coord == NULL || PySet_Add(result, coord) < 0) {
            Py_XDECREF(coord);
            Py_DECREF(result);
            free(coords);
            return NULL;
        }
        Py_DECREF(coord);
    }

    free(coords);
    return result;
}

static PyObject* py_reachable_space(PyObject* self, PyObject* args) {
    (void)self;
    PyObject* board_obj = NULL;
    PyObject* start_obj = NULL;
    const char* snake_id = NULL;
    if (!PyArg_ParseTuple(args, "OOs", &board_obj, &start_obj, &snake_id)) {
        return NULL;
    }

    Board* board = board_from_pyobject(board_obj);
    Coord start;
    if (board == NULL || PyCoord_AsCoord(start_obj, &start) < 0) {
        return NULL;
    }

    int out_count = 0;
    CoreStatus status = CoreReachableSpace(board, start, snake_id, &out_count);
    if (status != CORE_OK) {
        return raise_for_status(status);
    }
    return PyLong_FromLong(out_count);
}

static PyObject* py_shortest_path(PyObject* self, PyObject* args) {
    (void)self;
    PyObject* board_obj = NULL;
    PyObject* start_obj = NULL;
    PyObject* goal_obj = NULL;
    const char* snake_id = NULL;
    if (!PyArg_ParseTuple(args, "OOOs", &board_obj, &start_obj, &goal_obj, &snake_id)) {
        return NULL;
    }

    Board* board = board_from_pyobject(board_obj);
    Coord start;
    Coord goal;
    if (board == NULL || PyCoord_AsCoord(start_obj, &start) < 0 || PyCoord_AsCoord(goal_obj, &goal) < 0) {
        return NULL;
    }

    Coord* out_path = NULL;
    int out_path_count = 0;
    CoreStatus status = CoreShortestPath(board, start, goal, snake_id, &out_path, &out_path_count);
    if (status != CORE_OK) {
        return raise_for_status(status);
    }

    PyObject* result = PyList_New(out_path_count);
    if (result == NULL) {
        free(out_path);
        return NULL;
    }
    for (int i = 0; i < out_path_count; i++) {
        PyObject* coord = PyCoord_FromCoord(out_path[i]);
        if (coord == NULL) {
            Py_DECREF(result);
            free(out_path);
            return NULL;
        }
        PyList_SET_ITEM(result, i, coord);
    }
    free(out_path);
    return result;
}

static PyObject* py_voronoi_territory(PyObject* self, PyObject* args) {
    (void)self;
    PyObject* board_obj = NULL;
    if (!PyArg_ParseTuple(args, "O", &board_obj)) {
        return NULL;
    }

    Board* board = board_from_pyobject(board_obj);
    if (board == NULL) {
        return NULL;
    }

    CoreTerritory* out_territory = NULL;
    CoreStatus status = CoreVoronoiTerritory(board, (void**)&out_territory);
    if (status != CORE_OK) {
        return raise_for_status(status);
    }

    PyObject* result = PyDict_New();
    if (result == NULL) {
        CoreTerritoryFree(out_territory);
        return NULL;
    }

    for (int i = 0; i < out_territory->entry_count; i++) {
        CoreTerritoryEntry* entry = &out_territory->entries[i];
        PyObject* coords = PySet_New(NULL);
        if (coords == NULL) {
            Py_DECREF(result);
            CoreTerritoryFree(out_territory);
            return NULL;
        }

        for (int j = 0; j < entry->coord_count; j++) {
            PyObject* coord = PyCoord_FromCoord(entry->coords[j]);
            if (coord == NULL || PySet_Add(coords, coord) < 0) {
                Py_XDECREF(coord);
                Py_DECREF(coords);
                Py_DECREF(result);
                CoreTerritoryFree(out_territory);
                return NULL;
            }
            Py_DECREF(coord);
        }

        if (PyDict_SetItemString(result, entry->snake_id, coords) < 0) {
            Py_DECREF(coords);
            Py_DECREF(result);
            CoreTerritoryFree(out_territory);
            return NULL;
        }
        Py_DECREF(coords);
    }

    CoreTerritoryFree(out_territory);
    return result;
}

static int parse_parallel_mode(const char* value, CoreSearchParallelMode* out_mode) {
    if (value == NULL || strcmp(value, "serial") == 0) {
        *out_mode = CORE_SEARCH_PARALLEL_SERIAL;
        return 0;
    }
    if (strcmp(value, "root_moves") == 0) {
        *out_mode = CORE_SEARCH_PARALLEL_ROOT_MOVES;
        return 0;
    }
    if (strcmp(value, "pv_root_moves") == 0) {
        *out_mode = CORE_SEARCH_PARALLEL_PV_ROOT_MOVES;
        return 0;
    }
    if (strcmp(value, "root_replies") == 0) {
        *out_mode = CORE_SEARCH_PARALLEL_ROOT_REPLIES;
        return 0;
    }
    if (strcmp(value, "ply1_tasks") == 0) {
        *out_mode = CORE_SEARCH_PARALLEL_PLY1_TASKS;
        return 0;
    }
    if (strcmp(value, "leaf_eval") == 0) {
        *out_mode = CORE_SEARCH_PARALLEL_LEAF_EVAL;
        return 0;
    }
    PyErr_Format(PyExc_ValueError, "unknown minimax parallel_mode: %s", value);
    return -1;
}

static int parse_root_policy(const char* value, CoreRootPolicy* out_policy) {
    if (strcmp(value, "strict_minimax") == 0) {
        *out_policy = CORE_ROOT_POLICY_STRICT_MINIMAX;
        return 0;
    }
    if (strcmp(value, "standard_ladder_opportunity") == 0) {
        *out_policy = CORE_ROOT_POLICY_STANDARD_LADDER_OPPORTUNITY;
        return 0;
    }
    PyErr_Format(PyExc_ValueError, "unknown root_policy: %s", value);
    return -1;
}

static const char* outcome_name(CoreOutcome outcome) {
    switch (outcome) {
        case CORE_OUTCOME_WIN: return "win";
        case CORE_OUTCOME_DRAW: return "draw";
        case CORE_OUTCOME_LOSS: return "loss";
        default: return "unresolved";
    }
}

static const char* bound_name(CoreValueBound bound) {
    switch (bound) {
        case CORE_VALUE_BOUND_LOWER: return "lower";
        case CORE_VALUE_BOUND_UPPER: return "upper";
        default: return "exact";
    }
}

static const char* trap_status_name(CoreTrapStatus status) {
    switch (status) {
        case CORE_TRAP_IMMEDIATE_DEATH: return "immediate_death";
        case CORE_TRAP_PROVEN_SELF_TRAP: return "proven_self_trap";
        case CORE_TRAP_OPEN_BRANCH: return "open_branch";
        case CORE_TRAP_SURVIVES_CYCLE: return "survives_cycle";
        case CORE_TRAP_SURVIVES_HORIZON: return "survives_horizon";
        case CORE_TRAP_UNKNOWN: return "unknown";
        default: return "not_analyzed";
    }
}

static const char* structural_proof_name(CoreStructuralProofResult result) {
    switch (result) {
        case CORE_STRUCTURAL_PROOF_SAFE: return "safe";
        case CORE_STRUCTURAL_PROOF_UNSAFE: return "unsafe";
        case CORE_STRUCTURAL_PROOF_UNKNOWN: return "unknown";
        default: return "not_analyzed";
    }
}

static const char* structural_cutoff_name(CoreStructuralProofCutoff cutoff) {
    switch (cutoff) {
        case CORE_STRUCTURAL_CUTOFF_CAPACITY: return "capacity";
        case CORE_STRUCTURAL_CUTOFF_CYCLE: return "cycle";
        case CORE_STRUCTURAL_CUTOFF_HORIZON: return "horizon";
        case CORE_STRUCTURAL_CUTOFF_DEAD_END: return "dead_end";
        case CORE_STRUCTURAL_CUTOFF_DEADLINE: return "deadline";
        case CORE_STRUCTURAL_CUTOFF_RESOURCE_LIMIT: return "resource_limit";
        case CORE_STRUCTURAL_CUTOFF_ALLOCATION_FAILURE: return "allocation_failure";
        case CORE_STRUCTURAL_CUTOFF_POLICY_SUFFICIENT: return "policy_sufficient";
        case CORE_STRUCTURAL_CUTOFF_SURVIVABILITY: return "survivability";
        case CORE_STRUCTURAL_CUTOFF_BOUNDED_LASSO: return "bounded_lasso";
        default: return "none";
    }
}

static const char* refutation_status_name(CoreRefutationStatus status) {
    switch (status) {
        case CORE_REFUTATION_PROVEN_REFUTABLE: return "proven_refutable";
        case CORE_REFUTATION_NOT_REFUTABLE: return "not_refutable";
        case CORE_REFUTATION_UNKNOWN: return "unknown";
        default: return "not_analyzed";
    }
}

static const char* rejection_reason_name(CoreRootRejectionReason reason) {
    switch (reason) {
        case CORE_ROOT_REJECTION_NO_SURVIVING_REPLY: return "no_surviving_reply";
        case CORE_ROOT_REJECTION_PROVEN_SHORT_SELF_TRAP: return "proven_short_self_trap";
        case CORE_ROOT_REJECTION_STRUCTURALLY_DOMINATED: return "structurally_dominated";
        default: return "none";
    }
}

static const char* root_policy_name(CoreRootPolicy policy) {
    return policy == CORE_ROOT_POLICY_STANDARD_LADDER_OPPORTUNITY ?
        "standard_ladder_opportunity" : "strict_minimax";
}

static const char* selection_reason_name(CoreSelectionReason reason) {
    switch (reason) {
        case CORE_SELECTION_TIMEOUT_BEST_SO_FAR: return "timeout_best_so_far";
        case CORE_SELECTION_NODE_BUDGET_BEST_SO_FAR: return "node_budget_best_so_far";
        case CORE_SELECTION_ALLOWED_FALLBACK: return "allowed_fallback";
        case CORE_SELECTION_CORRIDOR_GUARD: return "corridor_guard";
        default: return "minimax";
    }
}

static const char* root_comparison_reason_name(CoreRootComparisonReason reason) {
    switch (reason) {
        case CORE_ROOT_COMPARISON_TERMINAL_OUTCOME: return "terminal_outcome";
        case CORE_ROOT_COMPARISON_SEARCH_BOUND: return "search_bound";
        case CORE_ROOT_COMPARISON_STRUCTURAL_PROOF: return "structural_proof";
        case CORE_ROOT_COMPARISON_TERMINAL_SURVIVAL: return "terminal_survival";
        case CORE_ROOT_COMPARISON_HEURISTIC_VALUE: return "heuristic_value";
        case CORE_ROOT_COMPARISON_STRUCTURAL_TIEBREAK: return "structural_tiebreak";
        case CORE_ROOT_COMPARISON_PREVIOUS_PV: return "previous_pv";
        case CORE_ROOT_COMPARISON_STABLE_DIRECTION: return "stable_direction";
        case CORE_ROOT_COMPARISON_CORRIDOR_GUARD: return "corridor_guard";
        case CORE_ROOT_COMPARISON_NUMERIC_VALUE: return "numeric_value";
        default: return "not_compared";
    }
}

static const char* root_comparison_ordering_name(CoreRootComparisonOrdering ordering) {
    switch (ordering) {
        case CORE_ROOT_COMPARISON_INCUMBENT: return "incumbent";
        case CORE_ROOT_COMPARISON_EQUAL: return "equal";
        case CORE_ROOT_COMPARISON_CANDIDATE: return "candidate";
        case CORE_ROOT_COMPARISON_INCOMPARABLE: return "incomparable";
    }
    return "incomparable";
}

static const char* corridor_guard_decision_name(CoreCorridorGuardDecision decision) {
    switch (decision) {
        case CORE_CORRIDOR_GUARD_SAME_AS_INCUMBENT: return "same_as_incumbent";
        case CORE_CORRIDOR_GUARD_REJECTED_SEARCH_ORDER: return "rejected_search_order";
        case CORE_CORRIDOR_GUARD_APPLIED_EXACT_TIE: return "applied_exact_tie";
        default: return "not_considered";
    }
}

static PyObject* cause_list(uint32_t causes) {
    static const struct {
        uint32_t bit;
        const char* name;
    } names[] = {
        {CORE_TERMINAL_CAUSE_WALL, "wall"},
        {CORE_TERMINAL_CAUSE_SELF_BODY, "self_body"},
        {CORE_TERMINAL_CAUSE_OTHER_BODY, "other_body"},
        {CORE_TERMINAL_CAUSE_HEAD_TO_HEAD, "head_to_head"},
        {CORE_TERMINAL_CAUSE_STARVATION, "starvation"},
        {CORE_TERMINAL_CAUSE_HAZARD, "hazard"},
        {CORE_TERMINAL_CAUSE_INVALID_COMMAND, "invalid_command"},
        {CORE_TERMINAL_CAUSE_OPPONENT_ELIMINATED, "opponent_eliminated"},
    };
    PyObject* result = PyList_New(0);
    if (result == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if ((causes & names[i].bit) == 0) {
            continue;
        }
        PyObject* name = PyUnicode_FromString(names[i].name);
        if (name == NULL || PyList_Append(result, name) < 0) {
            Py_XDECREF(name);
            Py_DECREF(result);
            return NULL;
        }
        Py_DECREF(name);
    }
    return result;
}

static const char* reply_outcome_name(const CoreRootCandidateStats* candidate, int move) {
    uint8_t bit = (uint8_t)(1u << move);
    if ((candidate->win_reply_mask & bit) != 0) return "win";
    if ((candidate->draw_reply_mask & bit) != 0) return "draw";
    if ((candidate->loss_reply_mask & bit) != 0) return "loss";
    if ((candidate->both_alive_reply_mask & bit) != 0) return "both_alive";
    return NULL;
}

static PyObject* root_candidate_dict(const CoreRootCandidateStats* candidate) {
    PyObject* result = PyDict_New();
    PyObject* replies = PyDict_New();
    PyObject* causes = cause_list(candidate->immediate_causes);
    if (result == NULL || replies == NULL || causes == NULL) {
        Py_XDECREF(result);
        Py_XDECREF(replies);
        Py_XDECREF(causes);
        return NULL;
    }
    for (int move = MOVE_UP; move <= MOVE_RIGHT; move++) {
        const char* outcome = reply_outcome_name(candidate, move);
        if (outcome != NULL) {
            PyObject* value = PyUnicode_FromString(outcome);
            if (value == NULL || PyDict_SetItemString(replies, MoveDirectionToString((MoveDirection)move), value) < 0) {
                Py_XDECREF(value);
                Py_DECREF(result);
                Py_DECREF(replies);
                Py_DECREF(causes);
                return NULL;
            }
            Py_DECREF(value);
        }
    }
    int failed = dict_set_bool(result, "evaluated", candidate->evaluated) < 0 ||
        dict_set_bool(result, "allowed", candidate->allowed) < 0 ||
        dict_set_string(result, "rejection_reason", rejection_reason_name(candidate->rejection_reason)) < 0 ||
        dict_set_bool(result, "safe_by_board_rules", candidate->safe_by_board_rules) < 0 ||
        PyDict_SetItemString(result, "reply_outcomes", replies) < 0 ||
        dict_set_int(result, "alive_reply_mask", candidate->alive_reply_mask) < 0 ||
        dict_set_int(result, "alive_reply_count", candidate->alive_reply_count) < 0 ||
        dict_set_int(result, "draw_reply_mask", candidate->draw_reply_mask) < 0 ||
        PyDict_SetItemString(result, "immediate_causes", causes) < 0 ||
        dict_set_string(result, "trap_status", trap_status_name(candidate->trap_status)) < 0 ||
        dict_set_int(result, "trap_horizon", candidate->trap_horizon) < 0 ||
        dict_set_string(result, "structural_proof", structural_proof_name(candidate->structural_proof)) < 0 ||
        dict_set_string(result, "proof_cutoff", structural_cutoff_name(candidate->proof_cutoff)) < 0 ||
        dict_set_int(result, "proof_horizon", candidate->proof_horizon) < 0 ||
        dict_set_u64(result, "explored_states", candidate->explored_states) < 0 ||
        dict_set_int(result, "structural_capacity", candidate->structural_capacity) < 0 ||
        dict_set_bool(result, "opponent_closure_considered", candidate->opponent_closure_considered) < 0 ||
        dict_set_int(result, "post_move_length", candidate->post_move_length) < 0 ||
        dict_set_int(result, "relaxed_static_capacity", candidate->relaxed_static_capacity) < 0 ||
        dict_set_string(result, "refutation_status", refutation_status_name(candidate->refutation_status)) < 0;
    Py_DECREF(replies);
    Py_DECREF(causes);
    if (failed) {
        Py_DECREF(result);
        return NULL;
    }
    if (candidate->minimax_value_valid) {
        if (dict_set_double(result, "minimax_score", candidate->minimax_value.score) < 0 ||
            dict_set_string(result, "minimax_outcome", outcome_name(candidate->minimax_value.outcome)) < 0 ||
            dict_set_int(result, "minimax_terminal_distance", candidate->minimax_value.terminal_distance) < 0 ||
            dict_set_string(result, "minimax_bound", bound_name(candidate->minimax_value.bound)) < 0) {
            Py_DECREF(result);
            return NULL;
        }
        PyObject* terminal_causes = cause_list(candidate->minimax_value.cause);
        if (terminal_causes == NULL || PyDict_SetItemString(result, "minimax_cause", terminal_causes) < 0) {
            Py_XDECREF(terminal_causes);
            Py_DECREF(result);
            return NULL;
        }
        Py_DECREF(terminal_causes);
    } else {
        const char* fields[] = {
            "minimax_score", "minimax_outcome", "minimax_terminal_distance", "minimax_cause", "minimax_bound"
        };
        for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
            if (PyDict_SetItemString(result, fields[i], Py_None) < 0) {
                Py_DECREF(result);
                return NULL;
            }
        }
    }
    return result;
}

static PyObject* corridor_guard_candidate_dict(
    const CoreCorridorGuardCandidateAudit* candidate
) {
    PyObject* result = PyDict_New();
    PyObject* metrics = PyDict_New();
    if (result == NULL || metrics == NULL) {
        Py_XDECREF(result);
        Py_XDECREF(metrics);
        return NULL;
    }

    int failed = 0;
    if (candidate->corridor_metrics.valid) {
        failed = dict_set_int(
                metrics, "immediate_exits", candidate->corridor_metrics.immediate_exits
            ) < 0 ||
            dict_set_int(metrics, "forced_steps", candidate->corridor_metrics.forced_steps) < 0 ||
            dict_set_int(metrics, "reachable", candidate->corridor_metrics.reachable) < 0;
    } else {
        failed = dict_set_none(metrics, "immediate_exits") < 0 ||
            dict_set_none(metrics, "forced_steps") < 0 ||
            dict_set_none(metrics, "reachable") < 0;
    }
    if (!failed) {
        failed = PyDict_SetItemString(result, "corridor_metrics", metrics) < 0;
    }
    Py_DECREF(metrics);
    if (failed) {
        Py_DECREF(result);
        return NULL;
    }

    if (candidate->valid) {
        failed = dict_set_string(result, "move", MoveDirectionToString(candidate->move)) < 0 ||
            dict_set_string(
                result, "structural_proof", structural_proof_name(candidate->structural_proof)
            ) < 0 ||
            dict_set_int(
                result, "relaxed_static_capacity", candidate->relaxed_static_capacity
            ) < 0 ||
            dict_set_int(result, "post_move_length", candidate->post_move_length) < 0;
    } else {
        failed = dict_set_none(result, "move") < 0 ||
            dict_set_none(result, "structural_proof") < 0 ||
            dict_set_none(result, "relaxed_static_capacity") < 0 ||
            dict_set_none(result, "post_move_length") < 0;
    }
    if (!failed && candidate->minimax_value_valid) {
        failed = dict_set_double(result, "minimax_score", candidate->minimax_value.score) < 0 ||
            dict_set_string(
                result, "minimax_outcome", outcome_name(candidate->minimax_value.outcome)
            ) < 0 ||
            dict_set_string(result, "minimax_bound", bound_name(candidate->minimax_value.bound)) < 0;
    } else if (!failed) {
        failed = dict_set_none(result, "minimax_score") < 0 ||
            dict_set_none(result, "minimax_outcome") < 0 ||
            dict_set_none(result, "minimax_bound") < 0;
    }
    if (failed) {
        Py_DECREF(result);
        return NULL;
    }
    return result;
}

static PyObject* corridor_guard_dict(const CoreCorridorGuardAudit* audit) {
    PyObject* result = PyDict_New();
    PyObject* incumbent = corridor_guard_candidate_dict(&audit->incumbent);
    PyObject* proposal = corridor_guard_candidate_dict(&audit->proposal);
    if (result == NULL || incumbent == NULL || proposal == NULL) {
        Py_XDECREF(result);
        Py_XDECREF(incumbent);
        Py_XDECREF(proposal);
        return NULL;
    }
    int failed = dict_set_bool(result, "considered", audit->considered) < 0 ||
        PyDict_SetItemString(result, "incumbent", incumbent) < 0 ||
        PyDict_SetItemString(result, "proposal", proposal) < 0 ||
        dict_set_string(
            result,
            "comparison_ordering",
            root_comparison_ordering_name(audit->comparison_ordering)
        ) < 0 ||
        dict_set_string(
            result, "comparison_reason", root_comparison_reason_name(audit->comparison_reason)
        ) < 0 ||
        dict_set_bool(result, "exact_tie_permitted", audit->exact_tie_permitted) < 0 ||
        dict_set_bool(result, "applied", audit->applied) < 0 ||
        dict_set_string(result, "decision", corridor_guard_decision_name(audit->decision)) < 0;
    Py_DECREF(incumbent);
    Py_DECREF(proposal);
    if (failed) {
        Py_DECREF(result);
        return NULL;
    }
    return result;
}

static PyObject* py_minimax_move(PyObject* self, PyObject* args, PyObject* kwds) {
    (void)self;
    static char* kwlist[] = {"board", "snake_id", "time_budget_ms", "weights", "root_policy", NULL};
    PyObject* board_obj = NULL;
    PyObject* weights_obj = NULL;
    const char* snake_id = NULL;
    int time_budget_ms = 400;
    const char* root_policy = "standard_ladder_opportunity";
    if (!PyArg_ParseTupleAndKeywords(
            args,
            kwds,
            "Os|iOs",
            kwlist,
            &board_obj,
            &snake_id,
            &time_budget_ms,
            &weights_obj,
            &root_policy
        )) {
        return NULL;
    }

    Board* board = board_from_pyobject(board_obj);
    if (board == NULL) {
        return NULL;
    }

    MoveDirection out_move = MOVE_INVALID;
    CoreSearchConfig config = CoreSearchConfigDefault(time_budget_ms);
    if (parse_root_policy(root_policy, &config.root_policy) < 0) {
        return NULL;
    }
    if (parse_evaluation_weights(weights_obj, &config.weights) < 0) {
        return NULL;
    }

    CoreSearchStats stats;
    CoreStatus status = CoreMinimaxMoveWithStats(board, snake_id, config, &out_move, &stats);
    if (status != CORE_OK) {
        return raise_for_status(status);
    }
    return PyUnicode_FromString(MoveDirectionToString(out_move));
}

static PyObject* py_minimax_diagnostics(PyObject* self, PyObject* args, PyObject* kwds) {
    (void)self;
    static char* kwlist[] = {
        "board",
        "snake_id",
        "time_budget_ms",
        "fixed_depth",
        "enable_tt",
        "enable_move_ordering",
        "enable_make_unmake",
        "weights",
        "parallel_mode",
        "root_policy",
        "node_budget",
        NULL,
    };
    PyObject* board_obj = NULL;
    PyObject* weights_obj = NULL;
    PyObject* node_budget_obj = NULL;
    const char* snake_id = NULL;
    int time_budget_ms = 400;
    int fixed_depth = 0;
    int enable_tt = 1;
    int enable_move_ordering = 1;
    int enable_make_unmake = 1;
    const char* parallel_mode = "serial";
    const char* root_policy = "standard_ladder_opportunity";
    if (!PyArg_ParseTupleAndKeywords(
            args,
            kwds,
            "Os|iiiiiOssO",
            kwlist,
            &board_obj,
            &snake_id,
            &time_budget_ms,
            &fixed_depth,
            &enable_tt,
            &enable_move_ordering,
            &enable_make_unmake,
            &weights_obj,
            &parallel_mode,
            &root_policy,
            &node_budget_obj
        )) {
        return NULL;
    }

    Board* board = board_from_pyobject(board_obj);
    if (board == NULL) {
        return NULL;
    }
    if (fixed_depth < 0 || fixed_depth > CORE_SEARCH_MAX_DEPTH) {
        PyErr_Format(PyExc_ValueError, "fixed_depth must be between 0 and %d", CORE_SEARCH_MAX_DEPTH);
        return NULL;
    }

    CoreSearchConfig config = CoreSearchConfigDefault(time_budget_ms);
    if (node_budget_obj != NULL && node_budget_obj != Py_None) {
        if (!PyLong_Check(node_budget_obj)) {
            PyErr_SetString(PyExc_TypeError, "node_budget must be an integer");
            return NULL;
        }
        unsigned long long node_budget = PyLong_AsUnsignedLongLong(node_budget_obj);
        if (PyErr_Occurred()) {
            PyErr_Clear();
            PyErr_SetString(PyExc_ValueError, "node_budget must be between 0 and 18446744073709551615");
            return NULL;
        }
        config.node_budget = (uint64_t)node_budget;
    }
    config.fixed_depth = fixed_depth;
    config.enable_tt = enable_tt != 0;
    config.enable_move_ordering = enable_move_ordering != 0;
    config.enable_make_unmake = enable_make_unmake != 0;
    if (parse_parallel_mode(parallel_mode, &config.parallel_mode) < 0) {
        return NULL;
    }
    if (parse_root_policy(root_policy, &config.root_policy) < 0) {
        return NULL;
    }
    if (parse_evaluation_weights(weights_obj, &config.weights) < 0) {
        return NULL;
    }

    MoveDirection out_move = MOVE_INVALID;
    CoreSearchStats stats;
    CoreStatus status = CoreMinimaxMoveWithStats(board, snake_id, config, &out_move, &stats);
    if (status != CORE_OK) {
        return raise_for_status(status);
    }

    PyObject* result = PyDict_New();
    if (result == NULL) {
        return NULL;
    }

    if (dict_set_string(result, "move", MoveDirectionToString(stats.move)) < 0 ||
        dict_set_double(result, "score", stats.score) < 0 ||
        dict_set_double(result, "elapsed_ms", stats.elapsed_ms) < 0 ||
        dict_set_int(result, "parallel_mode", stats.parallel_mode) < 0 ||
        dict_set_int(result, "parallel_workers_used", stats.parallel_workers_used) < 0 ||
        dict_set_int(result, "completed_depth", stats.completed_depth) < 0 ||
        dict_set_int(result, "max_depth_started", stats.max_depth_started) < 0 ||
        dict_set_bool(result, "timed_out", stats.timed_out) < 0 ||
        dict_set_u64(result, "node_budget", stats.node_budget) < 0 ||
        dict_set_bool(result, "node_budget_exhausted", stats.node_budget_exhausted) < 0 ||
        dict_set_u64(result, "nodes", stats.nodes) < 0 ||
        dict_set_u64(result, "leaf_evals", stats.leaf_evals) < 0 ||
        dict_set_u64(result, "clone_calls", stats.clone_calls) < 0 ||
        dict_set_u64(result, "board_allocations", stats.board_allocations) < 0 ||
        dict_set_u64(result, "safe_move_calls", stats.safe_move_calls) < 0 ||
        dict_set_u64(result, "beta_cutoffs", stats.beta_cutoffs) < 0 ||
        dict_set_u64(result, "move_order_first_choice_cutoffs", stats.move_order_first_choice_cutoffs) < 0 ||
        dict_set_u64(result, "tt_probes", stats.tt_probes) < 0 ||
        dict_set_u64(result, "tt_hits", stats.tt_hits) < 0 ||
        dict_set_u64(result, "tt_exact_hits", stats.tt_exact_hits) < 0 ||
        dict_set_u64(result, "tt_lower_hits", stats.tt_lower_hits) < 0 ||
        dict_set_u64(result, "tt_upper_hits", stats.tt_upper_hits) < 0 ||
        dict_set_u64(result, "tt_cutoffs", stats.tt_cutoffs) < 0 ||
        dict_set_u64(result, "tt_stores", stats.tt_stores) < 0 ||
        dict_set_u64(result, "tt_collisions", stats.tt_collisions) < 0 ||
        dict_set_int(result, "root_allowed_mask", stats.root_allowed_mask) < 0 ||
        dict_set_string(result, "root_policy_applied", root_policy_name(stats.root_policy_applied)) < 0 ||
        dict_set_string(result, "selection_reason", selection_reason_name(stats.selection_reason)) < 0 ||
        dict_set_string(result, "root_comparison_reason", root_comparison_reason_name(stats.root_comparison_reason)) < 0 ||
        dict_set_u64(result, "root_analysis_nodes", stats.root_analysis_nodes) < 0 ||
        dict_set_double(result, "root_analysis_elapsed_ms", stats.root_analysis_elapsed_ms) < 0 ||
        dict_set_int(result, "root_analysis_budget_ms", stats.root_analysis_budget_ms) < 0 ||
        dict_set_int(result, "search_reserved_ms", stats.search_reserved_ms) < 0) {
        Py_DECREF(result);
        return NULL;
    }

    PyObject* root_scores = PyDict_New();
    if (root_scores == NULL) {
        Py_DECREF(result);
        return NULL;
    }
    for (int move = MOVE_UP; move <= MOVE_RIGHT; move++) {
        if (!stats.root_move_score_valid[move]) {
            continue;
        }
        PyObject* value = PyFloat_FromDouble(stats.root_move_scores[move]);
        if (value == NULL ||
            PyDict_SetItemString(root_scores, MoveDirectionToString((MoveDirection)move), value) < 0) {
            Py_XDECREF(value);
            Py_DECREF(root_scores);
            Py_DECREF(result);
            return NULL;
        }
        Py_DECREF(value);
    }
    if (PyDict_SetItemString(result, "root_move_scores", root_scores) < 0) {
        Py_DECREF(root_scores);
        Py_DECREF(result);
        return NULL;
    }
    Py_DECREF(root_scores);

    PyObject* root_candidates = PyDict_New();
    if (root_candidates == NULL) {
        Py_DECREF(result);
        return NULL;
    }
    for (int move = MOVE_UP; move <= MOVE_RIGHT; move++) {
        PyObject* candidate = root_candidate_dict(&stats.root_candidates[move]);
        if (candidate == NULL ||
            PyDict_SetItemString(root_candidates, MoveDirectionToString((MoveDirection)move), candidate) < 0) {
            Py_XDECREF(candidate);
            Py_DECREF(root_candidates);
            Py_DECREF(result);
            return NULL;
        }
        Py_DECREF(candidate);
    }
    if (PyDict_SetItemString(result, "root_candidates", root_candidates) < 0) {
        Py_DECREF(root_candidates);
        Py_DECREF(result);
        return NULL;
    }
    Py_DECREF(root_candidates);

    PyObject* corridor_guard = corridor_guard_dict(&stats.corridor_guard);
    if (corridor_guard == NULL ||
        PyDict_SetItemString(result, "corridor_guard", corridor_guard) < 0) {
        Py_XDECREF(corridor_guard);
        Py_DECREF(result);
        return NULL;
    }
    Py_DECREF(corridor_guard);

    return result;
}

static PyObject* py_duel_root_profile(PyObject* self, PyObject* args, PyObject* kwds) {
    (void)self;
    static char* kwlist[] = {"board", "snake_id", NULL};
    PyObject* board_obj = NULL;
    const char* snake_id = NULL;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "Os", kwlist, &board_obj, &snake_id)) {
        return NULL;
    }
    Board* board = board_from_pyobject(board_obj);
    if (board == NULL) {
        return NULL;
    }
    CoreDuelRootProfileResult profile;
    CoreStatus status = CoreDuelRootProfile(board, snake_id, &profile);
    if (status != CORE_OK) {
        return raise_for_status(status);
    }
    PyObject* result = PyDict_New();
    if (result == NULL) {
        return NULL;
    }
    for (int move = MOVE_UP; move <= MOVE_RIGHT; move++) {
        const CoreDuelRootCommandProfile* source = &profile.commands[move];
        CoreRootCandidateStats candidate;
        memset(&candidate, 0, sizeof(candidate));
        candidate.evaluated = source->evaluated;
        candidate.allowed = true;
        candidate.safe_by_board_rules = source->safe_by_board_rules;
        candidate.opponent_reply_mask = source->opponent_reply_mask;
        candidate.win_reply_mask = source->win_reply_mask;
        candidate.draw_reply_mask = source->draw_reply_mask;
        candidate.both_alive_reply_mask = source->both_alive_reply_mask;
        candidate.loss_reply_mask = source->loss_reply_mask;
        candidate.alive_reply_mask = source->alive_reply_mask;
        candidate.alive_reply_count = source->alive_reply_count;
        candidate.immediate_causes = source->immediate_causes;
        PyObject* command = root_candidate_dict(&candidate);
        if (command == NULL || PyDict_SetItemString(result, MoveDirectionToString((MoveDirection)move), command) < 0) {
            Py_XDECREF(command);
            Py_DECREF(result);
            return NULL;
        }
        Py_DECREF(command);
    }
    return result;
}

static PyObject* py_space_time_metrics(PyObject* self, PyObject* args, PyObject* kwds) {
    (void)self;
    static char* kwlist[] = {"board", "snake_id", NULL};
    PyObject* board_obj = NULL;
    const char* snake_id = NULL;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "Os", kwlist, &board_obj, &snake_id)) {
        return NULL;
    }

    Board* board = board_from_pyobject(board_obj);
    if (board == NULL) {
        return NULL;
    }

    CoreSpaceTimeMetrics metrics;
    CoreStatus status = CoreSpaceTimeCompute(board, snake_id, &metrics);
    if (status != CORE_OK) {
        return raise_for_status(status);
    }

    PyObject* result = PyDict_New();
    if (result == NULL) {
        return NULL;
    }
    if (dict_set_int(result, "reachable_cells", metrics.reachable_cells) < 0 ||
        dict_set_int(result, "max_arrival", metrics.max_arrival) < 0 ||
        dict_set_bool(result, "tail_reachable", metrics.tail_reachable) < 0 ||
        dict_set_bool(result, "dead", metrics.dead) < 0) {
        Py_DECREF(result);
        return NULL;
    }
    return result;
}

static PyObject* py_standard_ffa_move(PyObject* self, PyObject* args, PyObject* kwds) {
    (void)self;
    static char* kwlist[] = {"board", "snake_id", "time_budget_ms", "theta", NULL};
    PyObject* board_obj = NULL;
    PyObject* theta_obj = NULL;
    const char* snake_id = NULL;
    int time_budget_ms = 80;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "Os|iO", kwlist, &board_obj, &snake_id, &time_budget_ms, &theta_obj)) {
        return NULL;
    }

    Board* board = board_from_pyobject(board_obj);
    if (board == NULL) {
        return NULL;
    }

    CoreStandardFfaConfig config = CoreStandardFfaConfigDefault(time_budget_ms);
    if (parse_standard_ffa_config(theta_obj, &config) < 0) {
        return NULL;
    }

    MoveDirection out_move = MOVE_INVALID;
    CoreStatus status = CoreStandardFfaMove(board, snake_id, &config, &out_move);
    if (status != CORE_OK) {
        return raise_for_status(status);
    }
    return PyUnicode_FromString(MoveDirectionToString(out_move));
}

static PyObject* py_choke_points(PyObject* self, PyObject* args) {
    (void)self;
    PyObject* board_obj = NULL;
    const char* snake_id = NULL;
    if (!PyArg_ParseTuple(args, "Os", &board_obj, &snake_id)) {
        return NULL;
    }

    Board* board = board_from_pyobject(board_obj);
    if (board == NULL) {
        return NULL;
    }

    Coord* out_points = NULL;
    int out_points_count = 0;
    CoreStatus status = CoreChokePoints(board, snake_id, &out_points, &out_points_count);
    if (status != CORE_OK) {
        return raise_for_status(status);
    }
    return coord_array_to_set_and_free(out_points, out_points_count);
}

static PyObject* py_edge_trap_move(PyObject* self, PyObject* args) {
    (void)self;
    PyObject* board_obj = NULL;
    const char* snake_id = NULL;
    if (!PyArg_ParseTuple(args, "Os", &board_obj, &snake_id)) {
        return NULL;
    }

    Board* board = board_from_pyobject(board_obj);
    if (board == NULL) {
        return NULL;
    }

    bool out_has_move = false;
    MoveDirection out_move = MOVE_INVALID;
    CoreStatus status = CoreEdgeTrapMove(board, snake_id, &out_has_move, &out_move);
    if (status != CORE_OK) {
        return raise_for_status(status);
    }
    if (!out_has_move) {
        Py_RETURN_NONE;
    }
    return PyUnicode_FromString(MoveDirectionToString(out_move));
}

static PyObject* py_predict_hazards(PyObject* self, PyObject* args, PyObject* kwds) {
    (void)self;
    static char* kwlist[] = {"board", "turns_ahead", NULL};
    PyObject* board_obj = NULL;
    int turns_ahead = 3;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|i", kwlist, &board_obj, &turns_ahead)) {
        return NULL;
    }

    Board* board = board_from_pyobject(board_obj);
    if (board == NULL) {
        return NULL;
    }

    Coord* out_hazards = NULL;
    int out_hazard_count = 0;
    CoreStatus status = CorePredictHazards(board, turns_ahead, &out_hazards, &out_hazard_count);
    if (status != CORE_OK) {
        return raise_for_status(status);
    }
    return coord_array_to_set_and_free(out_hazards, out_hazard_count);
}

static PyObject* py_evaluate(PyObject* self, PyObject* args, PyObject* kwds) {
    (void)self;
    static char* kwlist[] = {"board", "snake_id", "weights", NULL};
    PyObject* board_obj = NULL;
    PyObject* weights_obj = NULL;
    const char* snake_id = NULL;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "Os|O", kwlist, &board_obj, &snake_id, &weights_obj)) {
        return NULL;
    }

    Board* board = board_from_pyobject(board_obj);
    if (board == NULL) {
        return NULL;
    }

    double out_score = 0.0;
    CoreEvaluationWeights weights;
    if (parse_evaluation_weights(weights_obj, &weights) < 0) {
        return NULL;
    }

    CoreStatus status = CoreEvaluateWithWeights(board, snake_id, &weights, &out_score);
    if (status != CORE_OK) {
        return raise_for_status(status);
    }
    return PyFloat_FromDouble(out_score);
}

static PyObject* py_board_hash(PyObject* self, PyObject* args) {
    (void)self;
    PyObject* board_obj = NULL;
    if (!PyArg_ParseTuple(args, "O", &board_obj)) {
        return NULL;
    }

    Board* board = board_from_pyobject(board_obj);
    if (board == NULL) {
        return NULL;
    }

    return PyLong_FromUnsignedLongLong((unsigned long long)CoreZobristHashBoard(board));
}

static PyObject* duel_profile_weights_dict(const CoreEvaluationWeights* weights) {
    PyObject* result = PyDict_New();
    if (result == NULL) {
        return NULL;
    }
#define SET_WEIGHT(field) do { \
    if (dict_set_double(result, #field, weights->field) < 0) { \
        Py_DECREF(result); \
        return NULL; \
    } \
} while (0)
    SET_WEIGHT(terminal_win);
    SET_WEIGHT(terminal_loss);
    SET_WEIGHT(base);
    SET_WEIGHT(health);
    SET_WEIGHT(length);
    SET_WEIGHT(reachable_space);
    SET_WEIGHT(safe_moves);
    SET_WEIGHT(center);
    SET_WEIGHT(food);
    SET_WEIGHT(low_health_food);
    SET_WEIGHT(low_health_threshold);
    SET_WEIGHT(hazard_damage);
    SET_WEIGHT(hazard);
    SET_WEIGHT(length_advantage);
    SET_WEIGHT(adjacent_equal_or_longer_penalty);
    SET_WEIGHT(adjacent_shorter_bonus);
    SET_WEIGHT(opponent_reachable_space);
    SET_WEIGHT(territory_delta);
    SET_WEIGHT(opponent_safe_moves);
    SET_WEIGHT(opponent_low_health_food_denial);
#undef SET_WEIGHT
    return result;
}

static PyObject* py_duel_weight_profiles(PyObject* self, PyObject* args) {
    (void)self;
    (void)args;
    size_t count = CoreDuelWeightProfileCount();
    PyObject* result = PyList_New((Py_ssize_t)count);
    if (result == NULL) {
        return NULL;
    }
    for (size_t index = 0; index < count; ++index) {
        const CoreDuelWeightProfile* profile = CoreDuelWeightProfileAt(index);
        PyObject* record = PyDict_New();
        PyObject* weights = duel_profile_weights_dict(&profile->weights);
        if (record == NULL || weights == NULL ||
            dict_set_int(record, "schema_version", profile->schema_version) < 0 ||
            dict_set_string(record, "name", profile->name) < 0 ||
            dict_set_string(record, "version", profile->version) < 0 ||
            dict_set_string(record, "status", profile->status) < 0 ||
            dict_set_string(record, "sha256", profile->sha256) < 0 ||
            PyDict_SetItemString(record, "weights", weights) < 0) {
            Py_XDECREF(record);
            Py_XDECREF(weights);
            Py_DECREF(result);
            return NULL;
        }
        Py_DECREF(weights);
        PyList_SET_ITEM(result, (Py_ssize_t)index, record);
    }
    return result;
}

PyMethodDef PyCoreMethods[] = {
    {"reachable_space", py_reachable_space, METH_VARARGS, "Compute BFS flood-fill reachable space."},
    {"shortest_path", py_shortest_path, METH_VARARGS, "Compute an A* shortest path."},
    {"voronoi_territory", py_voronoi_territory, METH_VARARGS, "Compute multi-source BFS territory control."},
    {"minimax_move", (PyCFunction)py_minimax_move, METH_VARARGS | METH_KEYWORDS, "Choose a move with simultaneous-move minimax heuristics."},
    {"minimax_diagnostics", (PyCFunction)py_minimax_diagnostics, METH_VARARGS | METH_KEYWORDS, "Choose a move and return minimax search diagnostics."},
    {"duel_root_profile", (PyCFunction)py_duel_root_profile, METH_VARARGS | METH_KEYWORDS, "Classify all duel root command/reply pairs."},
    {"space_time_metrics", (PyCFunction)py_space_time_metrics, METH_VARARGS | METH_KEYWORDS, "Time-aware reachable-region metrics for a snake."},
    {"standard_ffa_move", (PyCFunction)py_standard_ffa_move, METH_VARARGS | METH_KEYWORDS, "Choose a move with the native Standard FFA strategy."},
    {"choke_points", py_choke_points, METH_VARARGS, "Detect articulation-point choke cells."},
    {"edge_trap_move", py_edge_trap_move, METH_VARARGS, "Choose an optional edge-trapping move."},
    {"predict_hazards", (PyCFunction)py_predict_hazards, METH_VARARGS | METH_KEYWORDS, "Predict Royale hazard cells."},
    {"evaluate", (PyCFunction)py_evaluate, METH_VARARGS | METH_KEYWORDS, "Evaluate board utility for one snake."},
    {"duel_weight_profiles", py_duel_weight_profiles, METH_NOARGS, "Return immutable compiled duel weight profile audit data."},
    {"board_hash", py_board_hash, METH_VARARGS, "Return deterministic 64-bit Zobrist hash for a board."},
    {NULL, NULL, 0, NULL}
};
