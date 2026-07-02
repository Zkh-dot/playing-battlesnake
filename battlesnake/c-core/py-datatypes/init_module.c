#include "py_datatypes.h"
#include "../py-core/py_core.h"

PyMODINIT_FUNC PyInit_battlesnake_native(void) {
    if (PyType_Ready(&PyCoordType) < 0) {
        return NULL;
    }
    if (PyType_Ready(&PySnakeType) < 0) {
        return NULL;
    }
    if (PyType_Ready(&PyBoardType) < 0) {
        return NULL;
    }

    static struct PyModuleDef moduledef = {
        PyModuleDef_HEAD_INIT,
        "battlesnake_native",
        "C-backed Battlesnake datatypes and core algorithm stubs",
        -1,
        NULL,
    };

    PyObject* module = PyModule_Create(&moduledef);
    if (module == NULL) {
        return NULL;
    }

    Py_INCREF(&PyCoordType);
    if (PyModule_AddObject(module, "Coord", (PyObject*)&PyCoordType) < 0) {
        Py_DECREF(&PyCoordType);
        Py_DECREF(module);
        return NULL;
    }

    Py_INCREF(&PySnakeType);
    if (PyModule_AddObject(module, "Snake", (PyObject*)&PySnakeType) < 0) {
        Py_DECREF(&PySnakeType);
        Py_DECREF(module);
        return NULL;
    }

    Py_INCREF(&PyBoardType);
    if (PyModule_AddObject(module, "Board", (PyObject*)&PyBoardType) < 0) {
        Py_DECREF(&PyBoardType);
        Py_DECREF(module);
        return NULL;
    }

    PyModule_AddStringConstant(module, "UP", "up");
    PyModule_AddStringConstant(module, "DOWN", "down");
    PyModule_AddStringConstant(module, "LEFT", "left");
    PyModule_AddStringConstant(module, "RIGHT", "right");
    if (PyModule_AddFunctions(module, PyCoreMethods) < 0) {
        Py_DECREF(module);
        return NULL;
    }
    return module;
}
