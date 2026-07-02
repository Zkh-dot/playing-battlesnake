#pragma once

#include <Python.h>
#include <stdbool.h>
#include <structmember.h>

#include "../datatypes/board.h"

typedef struct {
    PyObject_HEAD
    Coord coord;
} PyCoord;

typedef struct {
    PyObject_HEAD
    Snake* snake;
    bool owns_snake;
} PySnake;

typedef struct {
    PyObject_HEAD
    Board* board;
    bool owns_board;
} PyBoard;

extern PyTypeObject PyCoordType;
extern PyTypeObject PySnakeType;
extern PyTypeObject PyBoardType;

PyObject* PyCoord_FromCoord(Coord coord);
PyObject* PySnake_FromSnake(const Snake* snake);
PyObject* PyBoard_FromBoard(Board* board);

int PyCoord_AsCoord(PyObject* object, Coord* coord);
int PySnake_AsSnakeCopy(PyObject* object, Snake* snake);
int PyMove_AsDirection(PyObject* object, MoveDirection* move);

