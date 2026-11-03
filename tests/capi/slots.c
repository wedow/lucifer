/* PyModuleDef slots the loader must honour or refuse, as an unmodified
   extension: one shared object, one PyInit_ per module, each loaded through a
   file of its own name (tests/capi/slots_run.py). */

#include <Python.h>

static int
exec_set(PyObject *m)
{
    return PyModule_Add(m, "executed", PyLong_FromLong(1));
}

static PyObject *
create(PyObject *spec, PyModuleDef *def)
{
    PyObject *name = PyObject_GetAttrString(spec, "name");
    if (name == NULL) {
        return NULL;
    }
    PyObject *m = PyModule_NewObject(name);
    Py_DECREF(name);
    if (m != NULL && PyModule_Add(m, "created_by", PyUnicode_FromString("Py_mod_create")) < 0) {
        Py_CLEAR(m);
    }
    return m;
}

static PyObject *
create_not_a_module(PyObject *spec, PyModuleDef *def)
{
    return PyLong_FromLong(7);
}

static PyObject *
create_fails(PyObject *spec, PyModuleDef *def)
{
    return NULL;
}

static PyObject *
create_raises(PyObject *spec, PyModuleDef *def)
{
    PyErr_SetString(PyExc_KeyError, "from create");
    return NULL;
}

static int
exec_fails(PyObject *m)
{
    return -1;
}

static int
exec_raises(PyObject *m)
{
    PyErr_SetString(PyExc_ValueError, "from exec");
    return -1;
}

static PyObject *
state_get(PyObject *m, PyObject *unused)
{
    int *state = PyModule_GetState(m);
    return PyLong_FromLong(state == NULL ? -1 : (*state)++);
}

static PyMethodDef methods[] = {
    { "state", state_get, METH_NOARGS, "the module state, counting up" },
    { NULL, NULL, 0, NULL },
};

#define SUPPORTED { Py_mod_multiple_interpreters, Py_MOD_PER_INTERPRETER_GIL_SUPPORTED }
#define NOGIL { Py_mod_gil, Py_MOD_GIL_NOT_USED }
#define MODULE(name, ...) \
    static PyModuleDef_Slot name##_slots[] = { __VA_ARGS__, { 0, NULL } }; \
    static PyModuleDef name##_def = { PyModuleDef_HEAD_INIT, #name, "doc of " #name, \
                                      sizeof(int), methods, name##_slots, NULL, NULL, NULL }; \
    PyMODINIT_FUNC PyInit_##name(void) { return PyModuleDef_Init(&name##_def); }

MODULE(slots_plain, NOGIL, SUPPORTED, { Py_mod_exec, exec_set })
MODULE(slots_noshare, NOGIL, { Py_mod_multiple_interpreters, Py_MOD_MULTIPLE_INTERPRETERS_NOT_SUPPORTED })
MODULE(slots_shared_gil, NOGIL, { Py_mod_multiple_interpreters, Py_MOD_MULTIPLE_INTERPRETERS_SUPPORTED })
MODULE(slots_create, NOGIL, { Py_mod_create, create }, { Py_mod_exec, exec_set })
MODULE(slots_two_exec, NOGIL, { Py_mod_exec, exec_set }, { Py_mod_exec, exec_set })
MODULE(slots_unknown, NOGIL, { 99, NULL })
MODULE(slots_negative, NOGIL, { -1, NULL })
MODULE(slots_two_create, NOGIL, { Py_mod_create, create }, { Py_mod_create, create })
MODULE(slots_two_interp, NOGIL, SUPPORTED, SUPPORTED)
MODULE(slots_two_gil, NOGIL, NOGIL)
MODULE(slots_not_module, NOGIL, { Py_mod_create, create_not_a_module })
MODULE(slots_not_module_exec, NOGIL, { Py_mod_create, create_not_a_module }, { Py_mod_exec, exec_set })
MODULE(slots_create_fails, NOGIL, { Py_mod_create, create_fails })
MODULE(slots_create_raises, NOGIL, { Py_mod_create, create_raises })
MODULE(slots_exec_fails, NOGIL, { Py_mod_exec, exec_fails })
MODULE(slots_exec_raises, NOGIL, { Py_mod_exec, exec_raises })
