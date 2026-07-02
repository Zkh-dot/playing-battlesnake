#include "py_datatypes.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static PyObject* get_attr_or_none(PyObject* object, const char* name) {
    PyObject* value = PyObject_GetAttrString(object, name);
    if (value == NULL) {
        PyErr_Clear();
        Py_RETURN_NONE;
    }
    return value;
}

static char* string_attr_copy_or_default(PyObject* object, const char* name, const char* fallback) {
    PyObject* value = get_attr_or_none(object, name);
    if (value == NULL || value == Py_None) {
        Py_XDECREF(value);
        return strdup(fallback);
    }
    const char* parsed = PyUnicode_AsUTF8(value);
    char* copied = parsed != NULL ? strdup(parsed) : strdup(fallback);
    Py_DECREF(value);
    if (copied == NULL) {
        PyErr_NoMemory();
        return NULL;
    }
    if (parsed == NULL) {
        PyErr_Clear();
    }
    return copied;
}

static int int_attr_or_default(PyObject* object, const char* name, int fallback) {
    PyObject* value = get_attr_or_none(object, name);
    if (value == NULL || value == Py_None) {
        Py_XDECREF(value);
        return fallback;
    }
    long parsed = PyLong_AsLong(value);
    Py_DECREF(value);
    if (PyErr_Occurred()) {
        PyErr_Clear();
        return fallback;
    }
    return (int)parsed;
}

int PyCoord_AsCoord(PyObject* object, Coord* coord) {
    if (PyObject_TypeCheck(object, &PyCoordType)) {
        *coord = ((PyCoord*)object)->coord;
        return 0;
    }

    PyObject* x_obj = PyObject_GetAttrString(object, "x");
    PyObject* y_obj = PyObject_GetAttrString(object, "y");
    if (x_obj == NULL || y_obj == NULL) {
        Py_XDECREF(x_obj);
        Py_XDECREF(y_obj);
        PyErr_SetString(PyExc_TypeError, "coord must have x and y attributes");
        return -1;
    }

    long x = PyLong_AsLong(x_obj);
    long y = PyLong_AsLong(y_obj);
    Py_DECREF(x_obj);
    Py_DECREF(y_obj);
    if (PyErr_Occurred()) {
        return -1;
    }
    coord->x = (int)x;
    coord->y = (int)y;
    return 0;
}

static int coord_sequence_to_array(PyObject* iterable, Coord** coords, int* count) {
    PyObject* sequence = PySequence_Fast(iterable, "body/coords must be iterable");
    if (sequence == NULL) {
        return -1;
    }

    Py_ssize_t size = PySequence_Fast_GET_SIZE(sequence);
    Coord* parsed = NULL;
    if (size > 0) {
        parsed = (Coord*)malloc((size_t)size * sizeof(Coord));
        if (parsed == NULL) {
            Py_DECREF(sequence);
            PyErr_NoMemory();
            return -1;
        }
    }

    for (Py_ssize_t i = 0; i < size; i++) {
        PyObject* item = PySequence_Fast_GET_ITEM(sequence, i);
        if (PyCoord_AsCoord(item, &parsed[i]) < 0) {
            free(parsed);
            Py_DECREF(sequence);
            return -1;
        }
    }

    Py_DECREF(sequence);
    *coords = parsed;
    *count = (int)size;
    return 0;
}

int PySnake_AsSnakeCopy(PyObject* object, Snake* snake) {
    if (PyObject_TypeCheck(object, &PySnakeType)) {
        SnakeCopy(snake, ((PySnake*)object)->snake);
        return 0;
    }

    PyObject* body_obj = PyObject_GetAttrString(object, "body");
    if (body_obj == NULL) {
        PyErr_SetString(PyExc_TypeError, "snake must have a body attribute");
        return -1;
    }

    Coord* body = NULL;
    int body_len = 0;
    if (coord_sequence_to_array(body_obj, &body, &body_len) < 0) {
        Py_DECREF(body_obj);
        return -1;
    }
    Py_DECREF(body_obj);

    char* id = string_attr_copy_or_default(object, "id", "");
    char* name = string_attr_copy_or_default(object, "name", "");
    if (id == NULL || name == NULL) {
        free(id);
        free(name);
        free(body);
        return -1;
    }
    int health = int_attr_or_default(object, "health", 100);
    SnakeInit(snake, id, name, health, body, body_len);
    int length = int_attr_or_default(object, "length", body_len);
    snake->length = length > 0 ? length : body_len;
    free(id);
    free(name);
    free(body);
    return 0;
}

int PyMove_AsDirection(PyObject* object, MoveDirection* move) {
    const char* value = NULL;
    if (PyUnicode_Check(object)) {
        value = PyUnicode_AsUTF8(object);
    } else {
        PyObject* value_obj = PyObject_GetAttrString(object, "value");
        if (value_obj != NULL) {
            value = PyUnicode_AsUTF8(value_obj);
            Py_DECREF(value_obj);
        } else {
            PyErr_Clear();
        }
    }

    if (value == NULL) {
        return -1;
    }
    *move = MoveDirectionFromString(value);
    if (*move == MOVE_INVALID) {
        PyErr_SetString(PyExc_ValueError, "move must be one of up, down, left, right");
        return -1;
    }
    return 0;
}

static PyObject* coord_new(PyTypeObject* type, PyObject* args, PyObject* kwds) {
    PyCoord* self = (PyCoord*)type->tp_alloc(type, 0);
    if (self != NULL) {
        self->coord = (Coord){0, 0};
    }
    return (PyObject*)self;
}

static int coord_init(PyCoord* self, PyObject* args, PyObject* kwds) {
    static char* kwlist[] = {"x", "y", NULL};
    int x = 0;
    int y = 0;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|ii", kwlist, &x, &y)) {
        return -1;
    }
    self->coord = (Coord){x, y};
    return 0;
}

static Py_hash_t coord_hash(PyCoord* self) {
    Py_hash_t x = (Py_hash_t)self->coord.x;
    Py_hash_t y = (Py_hash_t)self->coord.y;
    Py_hash_t value = x * 1000003 ^ y;
    return value == -1 ? -2 : value;
}

static PyObject* coord_repr(PyCoord* self) {
    return PyUnicode_FromFormat("Coord(x=%d, y=%d)", self->coord.x, self->coord.y);
}

static PyObject* coord_richcompare(PyObject* left, PyObject* right, int op) {
    if (op != Py_EQ && op != Py_NE) {
        Py_RETURN_NOTIMPLEMENTED;
    }
    Coord left_coord;
    Coord right_coord;
    if (PyCoord_AsCoord(left, &left_coord) < 0 || PyCoord_AsCoord(right, &right_coord) < 0) {
        PyErr_Clear();
        Py_RETURN_NOTIMPLEMENTED;
    }
    bool equal = CoordEquals(left_coord, right_coord);
    if ((op == Py_EQ && equal) || (op == Py_NE && !equal)) {
        Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
}

static PyMemberDef coord_members[] = {
    {"x", T_INT, offsetof(PyCoord, coord.x), 0, "x coordinate"},
    {"y", T_INT, offsetof(PyCoord, coord.y), 0, "y coordinate"},
    {NULL}
};

PyTypeObject PyCoordType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "battlesnake_native.Coord",
    .tp_doc = "C-backed Battlesnake coordinate",
    .tp_basicsize = sizeof(PyCoord),
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = coord_new,
    .tp_init = (initproc)coord_init,
    .tp_members = coord_members,
    .tp_hash = (hashfunc)coord_hash,
    .tp_repr = (reprfunc)coord_repr,
    .tp_richcompare = coord_richcompare,
};

PyObject* PyCoord_FromCoord(Coord coord) {
    PyCoord* object = (PyCoord*)PyObject_CallObject((PyObject*)&PyCoordType, NULL);
    if (object == NULL) {
        return NULL;
    }
    object->coord = coord;
    return (PyObject*)object;
}

static void snake_dealloc(PySnake* self) {
    if (self->owns_snake && self->snake != NULL) {
        SnakeFree(self->snake);
        free(self->snake);
    }
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyObject* snake_new(PyTypeObject* type, PyObject* args, PyObject* kwds) {
    PySnake* self = (PySnake*)type->tp_alloc(type, 0);
    if (self != NULL) {
        self->snake = NULL;
        self->owns_snake = true;
    }
    return (PyObject*)self;
}

static int snake_init(PySnake* self, PyObject* args, PyObject* kwds) {
    static char* kwlist[] = {"id", "name", "health", "body", "head", "length", NULL};
    const char* id = "";
    const char* name = "";
    int health = 100;
    PyObject* body_obj = NULL;
    PyObject* head_obj = Py_None;
    PyObject* length_obj = Py_None;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|ssiOOO", kwlist, &id, &name, &health, &body_obj, &head_obj, &length_obj)) {
        return -1;
    }

    Coord* body = NULL;
    int body_len = 0;
    if (body_obj != NULL && body_obj != Py_None) {
        if (coord_sequence_to_array(body_obj, &body, &body_len) < 0) {
            return -1;
        }
    } else if (head_obj != NULL && head_obj != Py_None) {
        body = (Coord*)malloc(sizeof(Coord));
        if (body == NULL) {
            PyErr_NoMemory();
            return -1;
        }
        if (PyCoord_AsCoord(head_obj, &body[0]) < 0) {
            free(body);
            return -1;
        }
        body_len = 1;
    }

    Snake* snake = (Snake*)malloc(sizeof(Snake));
    if (snake == NULL) {
        free(body);
        PyErr_NoMemory();
        return -1;
    }
    SnakeInit(snake, id, name, health, body, body_len);
    if (length_obj != NULL && length_obj != Py_None) {
        long length = PyLong_AsLong(length_obj);
        if (PyErr_Occurred()) {
            SnakeFree(snake);
            free(snake);
            free(body);
            return -1;
        }
        snake->length = (int)length;
    }
    free(body);

    if (self->owns_snake && self->snake != NULL) {
        SnakeFree(self->snake);
        free(self->snake);
    }
    self->snake = snake;
    self->owns_snake = true;
    return 0;
}

static PyObject* snake_get_id(PySnake* self, void* closure) {
    return PyUnicode_FromString(self->snake != NULL ? self->snake->id : "");
}

static PyObject* snake_get_name(PySnake* self, void* closure) {
    return PyUnicode_FromString(self->snake != NULL ? self->snake->name : "");
}

static PyObject* snake_get_health(PySnake* self, void* closure) {
    return PyLong_FromLong(self->snake != NULL ? self->snake->health : 0);
}

static PyObject* snake_get_length(PySnake* self, void* closure) {
    return PyLong_FromLong(self->snake != NULL ? self->snake->length : 0);
}

static PyObject* snake_get_head(PySnake* self, void* closure) {
    if (self->snake == NULL || self->snake->body_len == 0) {
        Py_RETURN_NONE;
    }
    return PyCoord_FromCoord(SnakeHead(self->snake));
}

static PyObject* snake_get_body(PySnake* self, void* closure) {
    int count = self->snake != NULL ? self->snake->body_len : 0;
    PyObject* list = PyList_New(count);
    if (list == NULL) {
        return NULL;
    }
    for (int i = 0; i < count; i++) {
        PyObject* coord = PyCoord_FromCoord(self->snake->body[i]);
        if (coord == NULL) {
            Py_DECREF(list);
            return NULL;
        }
        PyList_SET_ITEM(list, i, coord);
    }
    return list;
}

static PyGetSetDef snake_getsets[] = {
    {"id", (getter)snake_get_id, NULL, "snake id", NULL},
    {"name", (getter)snake_get_name, NULL, "snake name", NULL},
    {"health", (getter)snake_get_health, NULL, "snake health", NULL},
    {"length", (getter)snake_get_length, NULL, "snake length", NULL},
    {"head", (getter)snake_get_head, NULL, "snake head", NULL},
    {"body", (getter)snake_get_body, NULL, "snake body", NULL},
    {NULL}
};

PyTypeObject PySnakeType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "battlesnake_native.Snake",
    .tp_doc = "C-backed Battlesnake snake",
    .tp_basicsize = sizeof(PySnake),
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = snake_new,
    .tp_init = (initproc)snake_init,
    .tp_dealloc = (destructor)snake_dealloc,
    .tp_getset = snake_getsets,
};

PyObject* PySnake_FromSnake(const Snake* snake) {
    PySnake* object = (PySnake*)PyObject_CallObject((PyObject*)&PySnakeType, NULL);
    if (object == NULL) {
        return NULL;
    }
    if (object->owns_snake && object->snake != NULL) {
        SnakeFree(object->snake);
        free(object->snake);
        object->snake = NULL;
    }
    object->snake = (Snake*)malloc(sizeof(Snake));
    if (object->snake == NULL) {
        Py_DECREF(object);
        PyErr_NoMemory();
        return NULL;
    }
    SnakeCopy(object->snake, snake);
    object->owns_snake = true;
    return (PyObject*)object;
}

static void board_dealloc(PyBoard* self) {
    if (self->owns_board && self->board != NULL) {
        BoardFree(self->board);
    }
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyObject* board_new(PyTypeObject* type, PyObject* args, PyObject* kwds) {
    PyBoard* self = (PyBoard*)type->tp_alloc(type, 0);
    if (self != NULL) {
        self->board = NULL;
        self->owns_board = true;
    }
    return (PyObject*)self;
}

static int add_snakes_from_object(Board* board, PyObject* snakes_obj) {
    PyObject* iterable = snakes_obj;
    PyObject* values = NULL;
    if (PyDict_Check(snakes_obj)) {
        values = PyMapping_Values(snakes_obj);
        if (values == NULL) {
            return -1;
        }
        iterable = values;
    }

    PyObject* sequence = PySequence_Fast(iterable, "snakes must be a dict or iterable");
    Py_XDECREF(values);
    if (sequence == NULL) {
        return -1;
    }

    Py_ssize_t count = PySequence_Fast_GET_SIZE(sequence);
    for (Py_ssize_t i = 0; i < count; i++) {
        Snake snake;
        if (PySnake_AsSnakeCopy(PySequence_Fast_GET_ITEM(sequence, i), &snake) < 0) {
            Py_DECREF(sequence);
            return -1;
        }
        bool ok = BoardAddSnake(board, &snake);
        SnakeFree(&snake);
        if (!ok) {
            Py_DECREF(sequence);
            PyErr_NoMemory();
            return -1;
        }
    }

    Py_DECREF(sequence);
    return 0;
}

static int add_coords_from_object(Board* board, PyObject* coords_obj, bool hazards) {
    if (coords_obj == NULL || coords_obj == Py_None) {
        return 0;
    }
    PyObject* sequence = PySequence_Fast(coords_obj, "coords must be iterable");
    if (sequence == NULL) {
        return -1;
    }
    Py_ssize_t count = PySequence_Fast_GET_SIZE(sequence);
    for (Py_ssize_t i = 0; i < count; i++) {
        Coord coord;
        if (PyCoord_AsCoord(PySequence_Fast_GET_ITEM(sequence, i), &coord) < 0) {
            Py_DECREF(sequence);
            return -1;
        }
        bool ok = hazards ? BoardAddHazard(board, coord) : BoardAddFood(board, coord);
        if (!ok) {
            Py_DECREF(sequence);
            PyErr_NoMemory();
            return -1;
        }
    }
    Py_DECREF(sequence);
    return 0;
}

static int board_init(PyBoard* self, PyObject* args, PyObject* kwds) {
    static char* kwlist[] = {"width", "height", "snakes", "food", "hazards", "ruleset_name", "hazard_damage", NULL};
    int width = 0;
    int height = 0;
    PyObject* snakes_obj = NULL;
    PyObject* food_obj = Py_None;
    PyObject* hazards_obj = Py_None;
    const char* ruleset_name = "standard";
    int hazard_damage = 15;

    if (!PyArg_ParseTupleAndKeywords(
            args,
            kwds,
            "iiO|OOsi",
            kwlist,
            &width,
            &height,
            &snakes_obj,
            &food_obj,
            &hazards_obj,
            &ruleset_name,
            &hazard_damage
        )) {
        return -1;
    }

    Board* board = BoardCreate(width, height, ruleset_name, hazard_damage);
    if (board == NULL) {
        PyErr_NoMemory();
        return -1;
    }
    if (add_snakes_from_object(board, snakes_obj) < 0 || add_coords_from_object(board, food_obj, false) < 0 || add_coords_from_object(board, hazards_obj, true) < 0) {
        BoardFree(board);
        return -1;
    }

    if (self->owns_board && self->board != NULL) {
        BoardFree(self->board);
    }
    self->board = board;
    self->owns_board = true;
    return 0;
}

static PyObject* board_get_width(PyBoard* self, void* closure) {
    return PyLong_FromLong(self->board != NULL ? self->board->width : 0);
}

static PyObject* board_get_height(PyBoard* self, void* closure) {
    return PyLong_FromLong(self->board != NULL ? self->board->height : 0);
}

static PyObject* board_get_ruleset_name(PyBoard* self, void* closure) {
    return PyUnicode_FromString(self->board != NULL ? self->board->ruleset_name : "");
}

static PyObject* board_get_hazard_damage(PyBoard* self, void* closure) {
    return PyLong_FromLong(self->board != NULL ? self->board->hazard_damage : 0);
}

static PyObject* board_get_snakes(PyBoard* self, void* closure) {
    PyObject* dict = PyDict_New();
    if (dict == NULL) {
        return NULL;
    }
    for (int i = 0; self->board != NULL && i < self->board->snake_count; i++) {
        PyObject* snake = PySnake_FromSnake(&self->board->snakes[i]);
        if (snake == NULL || PyDict_SetItemString(dict, self->board->snakes[i].id, snake) < 0) {
            Py_XDECREF(snake);
            Py_DECREF(dict);
            return NULL;
        }
        Py_DECREF(snake);
    }
    return dict;
}

static PyObject* coord_array_to_set(const Coord* coords, int count) {
    PyObject* set = PySet_New(NULL);
    if (set == NULL) {
        return NULL;
    }
    for (int i = 0; i < count; i++) {
        PyObject* coord = PyCoord_FromCoord(coords[i]);
        if (coord == NULL || PySet_Add(set, coord) < 0) {
            Py_XDECREF(coord);
            Py_DECREF(set);
            return NULL;
        }
        Py_DECREF(coord);
    }
    return set;
}

static PyObject* board_get_food(PyBoard* self, void* closure) {
    if (self->board == NULL) {
        return PySet_New(NULL);
    }
    return coord_array_to_set(self->board->food, self->board->food_count);
}

static PyObject* board_get_hazards(PyBoard* self, void* closure) {
    if (self->board == NULL) {
        return PySet_New(NULL);
    }
    return coord_array_to_set(self->board->hazards, self->board->hazard_count);
}

static PyObject* board_in_bounds(PyBoard* self, PyObject* arg) {
    Coord coord;
    if (PyCoord_AsCoord(arg, &coord) < 0) {
        return NULL;
    }
    if (BoardInBounds(self->board, coord)) {
        Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
}

static PyObject* board_head(PyBoard* self, PyObject* arg) {
    const char* snake_id = PyUnicode_AsUTF8(arg);
    if (snake_id == NULL) {
        return NULL;
    }
    const Snake* snake = BoardFindSnakeConst(self->board, snake_id);
    if (snake == NULL || snake->body_len == 0) {
        PyErr_SetString(PyExc_KeyError, "snake id not found");
        return NULL;
    }
    return PyCoord_FromCoord(SnakeHead(snake));
}

static PyObject* board_step(PyBoard* self, PyObject* args) {
    PyObject* coord_obj = NULL;
    PyObject* move_obj = NULL;
    if (!PyArg_ParseTuple(args, "OO", &coord_obj, &move_obj)) {
        return NULL;
    }
    Coord coord;
    MoveDirection move;
    if (PyCoord_AsCoord(coord_obj, &coord) < 0 || PyMove_AsDirection(move_obj, &move) < 0) {
        return NULL;
    }
    return PyCoord_FromCoord(MoveStep(coord, move));
}

static PyObject* board_is_safe(PyBoard* self, PyObject* args) {
    PyObject* coord_obj = NULL;
    const char* snake_id = NULL;
    if (!PyArg_ParseTuple(args, "Os", &coord_obj, &snake_id)) {
        return NULL;
    }
    Coord coord;
    if (PyCoord_AsCoord(coord_obj, &coord) < 0) {
        return NULL;
    }
    if (BoardIsSafe(self->board, coord, snake_id)) {
        Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
}

static PyObject* board_safe_moves(PyBoard* self, PyObject* arg) {
    const char* snake_id = PyUnicode_AsUTF8(arg);
    if (snake_id == NULL) {
        return NULL;
    }
    MoveDirection moves[4];
    int count = BoardSafeMoves(self->board, snake_id, moves);
    PyObject* list = PyList_New(count);
    if (list == NULL) {
        return NULL;
    }
    for (int i = 0; i < count; i++) {
        PyObject* move = PyUnicode_FromString(MoveDirectionToString(moves[i]));
        if (move == NULL) {
            Py_DECREF(list);
            return NULL;
        }
        PyList_SET_ITEM(list, i, move);
    }
    return list;
}

static PyObject* board_occupied(PyBoard* self, PyObject* args, PyObject* kwds) {
    static char* kwlist[] = {"include_tails", NULL};
    int include_tails = 1;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|p", kwlist, &include_tails)) {
        return NULL;
    }
    int count = BoardOccupied(self->board, include_tails != 0, NULL, 0);
    Coord* coords = NULL;
    if (count > 0) {
        coords = (Coord*)malloc((size_t)count * sizeof(Coord));
        if (coords == NULL) {
            PyErr_NoMemory();
            return NULL;
        }
        BoardOccupied(self->board, include_tails != 0, coords, count);
    }
    PyObject* set = coord_array_to_set(coords, count);
    free(coords);
    return set;
}

static PyObject* board_clone_and_apply(PyBoard* self, PyObject* arg) {
    if (!PyDict_Check(arg)) {
        PyErr_SetString(PyExc_TypeError, "moves must be a dict of snake id to move");
        return NULL;
    }

    Py_ssize_t count = PyDict_Size(arg);
    const char** snake_ids = (const char**)calloc((size_t)count, sizeof(char*));
    MoveDirection* moves = (MoveDirection*)calloc((size_t)count, sizeof(MoveDirection));
    if (snake_ids == NULL || moves == NULL) {
        free(snake_ids);
        free(moves);
        PyErr_NoMemory();
        return NULL;
    }

    PyObject* key = NULL;
    PyObject* value = NULL;
    Py_ssize_t pos = 0;
    int index = 0;
    while (PyDict_Next(arg, &pos, &key, &value)) {
        snake_ids[index] = PyUnicode_AsUTF8(key);
        if (snake_ids[index] == NULL || PyMove_AsDirection(value, &moves[index]) < 0) {
            free(snake_ids);
            free(moves);
            return NULL;
        }
        index++;
    }

    Board* next = BoardCloneAndApply(self->board, snake_ids, moves, (int)count);
    free(snake_ids);
    free(moves);
    if (next == NULL) {
        PyErr_NoMemory();
        return NULL;
    }
    return PyBoard_FromBoard(next);
}

static PyGetSetDef board_getsets[] = {
    {"width", (getter)board_get_width, NULL, "board width", NULL},
    {"height", (getter)board_get_height, NULL, "board height", NULL},
    {"ruleset_name", (getter)board_get_ruleset_name, NULL, "ruleset name", NULL},
    {"hazard_damage", (getter)board_get_hazard_damage, NULL, "hazard damage", NULL},
    {"snakes", (getter)board_get_snakes, NULL, "snakes by id", NULL},
    {"food", (getter)board_get_food, NULL, "food coordinates", NULL},
    {"hazards", (getter)board_get_hazards, NULL, "hazard coordinates", NULL},
    {NULL}
};

static PyMethodDef board_methods[] = {
    {"in_bounds", (PyCFunction)board_in_bounds, METH_O, "Return whether a coord is inside the board"},
    {"head", (PyCFunction)board_head, METH_O, "Return a snake head coordinate"},
    {"step", (PyCFunction)board_step, METH_VARARGS, "Apply a move to a coordinate"},
    {"is_safe", (PyCFunction)board_is_safe, METH_VARARGS, "Return whether a coordinate is safe for a snake"},
    {"safe_moves", (PyCFunction)board_safe_moves, METH_O, "Return immediate safe moves for a snake"},
    {"occupied", (PyCFunction)board_occupied, METH_VARARGS | METH_KEYWORDS, "Return occupied cells"},
    {"clone_and_apply", (PyCFunction)board_clone_and_apply, METH_O, "Clone board and apply simultaneous moves"},
    {NULL}
};

PyTypeObject PyBoardType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "battlesnake_native.Board",
    .tp_doc = "C-backed Battlesnake board",
    .tp_basicsize = sizeof(PyBoard),
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = board_new,
    .tp_init = (initproc)board_init,
    .tp_dealloc = (destructor)board_dealloc,
    .tp_getset = board_getsets,
    .tp_methods = board_methods,
};

PyObject* PyBoard_FromBoard(Board* board) {
    PyBoard* object = (PyBoard*)PyBoardType.tp_alloc(&PyBoardType, 0);
    if (object == NULL) {
        BoardFree(board);
        return NULL;
    }
    object->board = board;
    object->owns_board = true;
    return (PyObject*)object;
}
