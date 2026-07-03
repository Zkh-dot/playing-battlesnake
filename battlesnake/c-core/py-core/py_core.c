#include "py_core.h"

#include "../core/core_algorithms.h"
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

static PyObject* py_minimax_move(PyObject* self, PyObject* args, PyObject* kwds) {
    (void)self;
    static char* kwlist[] = {"board", "snake_id", "time_budget_ms", "weights", NULL};
    PyObject* board_obj = NULL;
    PyObject* weights_obj = NULL;
    const char* snake_id = NULL;
    int time_budget_ms = 400;
    if (!PyArg_ParseTupleAndKeywords(
            args,
            kwds,
            "Os|iO",
            kwlist,
            &board_obj,
            &snake_id,
            &time_budget_ms,
            &weights_obj
        )) {
        return NULL;
    }

    Board* board = board_from_pyobject(board_obj);
    if (board == NULL) {
        return NULL;
    }

    MoveDirection out_move = MOVE_INVALID;
    CoreSearchConfig config = CoreSearchConfigDefault(time_budget_ms);
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
        NULL,
    };
    PyObject* board_obj = NULL;
    PyObject* weights_obj = NULL;
    const char* snake_id = NULL;
    int time_budget_ms = 400;
    int fixed_depth = 0;
    int enable_tt = 1;
    int enable_move_ordering = 1;
    int enable_make_unmake = 1;
    if (!PyArg_ParseTupleAndKeywords(
            args,
            kwds,
            "Os|iiiiiO",
            kwlist,
            &board_obj,
            &snake_id,
            &time_budget_ms,
            &fixed_depth,
            &enable_tt,
            &enable_move_ordering,
            &enable_make_unmake,
            &weights_obj
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
    config.fixed_depth = fixed_depth;
    config.enable_tt = enable_tt != 0;
    config.enable_move_ordering = enable_move_ordering != 0;
    config.enable_make_unmake = enable_make_unmake != 0;
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
        dict_set_int(result, "completed_depth", stats.completed_depth) < 0 ||
        dict_set_int(result, "max_depth_started", stats.max_depth_started) < 0 ||
        dict_set_bool(result, "timed_out", stats.timed_out) < 0 ||
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
        dict_set_u64(result, "tt_collisions", stats.tt_collisions) < 0) {
        Py_DECREF(result);
        return NULL;
    }

    return result;
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

PyMethodDef PyCoreMethods[] = {
    {"reachable_space", py_reachable_space, METH_VARARGS, "Compute BFS flood-fill reachable space."},
    {"shortest_path", py_shortest_path, METH_VARARGS, "Compute an A* shortest path."},
    {"voronoi_territory", py_voronoi_territory, METH_VARARGS, "Compute multi-source BFS territory control."},
    {"minimax_move", (PyCFunction)py_minimax_move, METH_VARARGS | METH_KEYWORDS, "Choose a move with simultaneous-move minimax heuristics."},
    {"minimax_diagnostics", (PyCFunction)py_minimax_diagnostics, METH_VARARGS | METH_KEYWORDS, "Choose a move and return minimax search diagnostics."},
    {"choke_points", py_choke_points, METH_VARARGS, "Detect articulation-point choke cells."},
    {"edge_trap_move", py_edge_trap_move, METH_VARARGS, "Choose an optional edge-trapping move."},
    {"predict_hazards", (PyCFunction)py_predict_hazards, METH_VARARGS | METH_KEYWORDS, "Predict Royale hazard cells."},
    {"evaluate", (PyCFunction)py_evaluate, METH_VARARGS | METH_KEYWORDS, "Evaluate board utility for one snake."},
    {"board_hash", py_board_hash, METH_VARARGS, "Return deterministic 64-bit Zobrist hash for a board."},
    {NULL, NULL, 0, NULL}
};
