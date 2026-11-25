/* A PEP 523 frame evaluation hook set and cleared again inside one call:
   no frame is evaluated while it is set. */
#include "Python.h"

static PyObject *
counting(PyThreadState *ts, struct _PyInterpreterFrame *f, int throwflag)
{
    return _PyEval_EvalFrameDefault(ts, f, throwflag);
}

static PyObject *
toggle(PyObject *m, PyObject *unused)
{
    PyInterpreterState *interp = PyInterpreterState_Get();
    _PyFrameEvalFunction old = _PyInterpreterState_GetEvalFrameFunc(interp);
    _PyInterpreterState_SetEvalFrameFunc(interp, counting);
    _PyInterpreterState_SetEvalFrameFunc(interp, old);
    Py_RETURN_NONE;
}

static PyMethodDef methods[] = {
    { "toggle", toggle, METH_NOARGS, NULL },
    { NULL, NULL, 0, NULL },
};

static PyModuleDef_Slot slots[] = {
    { Py_mod_gil, Py_MOD_GIL_NOT_USED },
    { 0, NULL },
};

static struct PyModuleDef def = { PyModuleDef_HEAD_INIT, "hook_toggle", NULL, 0, methods, slots };

PyMODINIT_FUNC
PyInit_hook_toggle(void)
{
    return PyModuleDef_Init(&def);
}
