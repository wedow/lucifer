/* A dict watcher that keeps a dict alive from its PyDict_EVENT_DEALLOCATED
   event, which CPython allows (dict_dealloc: _PyObject_ResurrectStart and
   _PyObject_ResurrectEnd around the event). */
#include "Python.h"

static int watcher_id = -1;
static PyObject *kept;

static int
keep(PyDict_WatchEvent event, PyObject *dict, PyObject *key, PyObject *value)
{
    if (event == PyDict_EVENT_DEALLOCATED && kept == NULL) {
        kept = Py_NewRef(dict);
    }
    return 0;
}

static PyObject *
watch(PyObject *m, PyObject *d)
{
    if (watcher_id < 0 && (watcher_id = PyDict_AddWatcher(keep)) < 0) {
        return NULL;
    }
    if (PyDict_Watch(watcher_id, d) < 0) {
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *
take(PyObject *m, PyObject *unused)
{
    PyObject *r = kept != NULL ? kept : Py_None;
    if (kept == NULL) {
        Py_INCREF(r);
    }
    kept = NULL;
    return r;
}

static PyMethodDef methods[] = {
    { "watch", watch, METH_O, NULL },
    { "take", take, METH_NOARGS, NULL },
    { NULL },
};

static PyModuleDef_Slot slots[] = {
    { Py_mod_gil, Py_MOD_GIL_NOT_USED },
    { 0, NULL },
};

static struct PyModuleDef def = { PyModuleDef_HEAD_INIT, "dict_watch_resurrect", NULL, 0, methods, slots };

PyMODINIT_FUNC
PyInit_dict_watch_resurrect(void)
{
    return PyModuleDef_Init(&def);
}
