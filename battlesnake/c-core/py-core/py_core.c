#include "py_core.h"

#include "../core/core_algorithms.h"
#include "../core/zobrist.h"
#include "../py-datatypes/py_datatypes.h"

#include <stdlib.h>

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
    static char* kwlist[] = {"board", "snake_id", "time_budget_ms", NULL};
    PyObject* board_obj = NULL;
    const char* snake_id = NULL;
    int time_budget_ms = 400;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "Os|i", kwlist, &board_obj, &snake_id, &time_budget_ms)) {
        return NULL;
    }

    Board* board = board_from_pyobject(board_obj);
    if (board == NULL) {
        return NULL;
    }

    MoveDirection out_move = MOVE_INVALID;
    CoreStatus status = CoreMinimaxMove(board, snake_id, time_budget_ms, &out_move);
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

static PyObject* py_evaluate(PyObject* self, PyObject* args) {
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

    double out_score = 0.0;
    CoreStatus status = CoreEvaluate(board, snake_id, &out_score);
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
    {"choke_points", py_choke_points, METH_VARARGS, "Detect articulation-point choke cells."},
    {"edge_trap_move", py_edge_trap_move, METH_VARARGS, "Choose an optional edge-trapping move."},
    {"predict_hazards", (PyCFunction)py_predict_hazards, METH_VARARGS | METH_KEYWORDS, "Predict Royale hazard cells."},
    {"evaluate", py_evaluate, METH_VARARGS, "Evaluate board utility for one snake."},
    {"board_hash", py_board_hash, METH_VARARGS, "Return deterministic 64-bit Zobrist hash for a board."},
    {NULL, NULL, 0, NULL}
};
