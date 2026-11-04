/* Atomic dict updates (Objects/dictobject.c PyDict_Pop, PyDict_SetDefault,
   PyDict_SetDefaultRef), and the slice index helpers (Objects/sliceobject.c). */

#include "lucifer.h"

PyObject *
_PyDict_NewPresized(Py_ssize_t minused)
{
    return PyDict_New();
}

int
PyDict_Pop(PyObject *dict, PyObject *key, PyObject **result)
{
    if (!PyDict_Check(dict)) {
        PyErr_BadInternalCall();
        if (result != NULL) {
            *result = NULL;
        }
        return -1;
    }
    PyObject *value = NULL;
    int found;
    if (PyDict_Size(dict) == 0) {
        /* empty: the key is not even hashed (pop_lock_held's fast path) */
        if (result != NULL) {
            *result = NULL;
        }
        return 0;
    }
    Py_BEGIN_CRITICAL_SECTION(dict);
    found = PyDict_GetItemRef(dict, key, &value);
    if (found > 0 && PyDict_DelItem(dict, key) < 0) {
        Py_CLEAR(value);
        found = -1;
    }
    Py_END_CRITICAL_SECTION();
    if (result != NULL) {
        *result = value;
    }
    else {
        Py_XDECREF(value);
    }
    return found;
}

int
PyDict_SetDefaultRef(PyObject *dict, PyObject *key, PyObject *default_value, PyObject **result)
{
    if (!PyDict_Check(dict)) {
        PyErr_BadInternalCall();
        if (result != NULL) {
            *result = NULL;
        }
        return -1;
    }
    PyObject *value = NULL;
    int found;
    Py_BEGIN_CRITICAL_SECTION(dict);
    found = PyDict_GetItemRef(dict, key, &value);
    if (found == 0) {
        if (PyDict_SetItem(dict, key, default_value) < 0) {
            found = -1;
        }
        else {
            value = Py_NewRef(default_value);
        }
    }
    Py_END_CRITICAL_SECTION();
    if (result != NULL) {
        *result = value;
    }
    else {
        Py_XDECREF(value);
    }
    return found;
}

PyObject *
PyDict_SetDefault(PyObject *dict, PyObject *key, PyObject *default_value)
{
    PyObject *value;
    if (PyDict_SetDefaultRef(dict, key, default_value, &value) < 0) {
        return NULL;
    }
    Py_DECREF(value);          /* borrowed: the dict holds it */
    return value;
}

/* The index a slice component holds, clamped to the Py_ssize_t range. */
static int
slice_index(PyObject *v, Py_ssize_t *out)
{
    if (!PyIndex_Check(v)) {
        PyErr_SetString(PyExc_TypeError,
                        "slice indices must be integers or None or have an __index__ method");
        return 0;
    }
    PyObject *i = PyNumber_Index(v);
    if (i == NULL) {
        return 0;
    }
    *out = PyLong_AsSsize_t(i);
    if (*out == -1 && PyErr_Occurred()) {
        if (!PyErr_ExceptionMatches(PyExc_OverflowError)) {
            Py_DECREF(i);
            return 0;
        }
        PyErr_Clear();
        *out = (((PyLongObject *)i)->long_value.lv_tag & 3) == 2 ? PY_SSIZE_T_MIN : PY_SSIZE_T_MAX;
    }
    Py_DECREF(i);
    return 1;
}

int
PySlice_Unpack(PyObject *_r, Py_ssize_t *start, Py_ssize_t *stop, Py_ssize_t *step)
{
    PySliceObject *r = (PySliceObject *)_r;
    if (r->step == Py_None) {
        *step = 1;
    }
    else {
        if (!slice_index(r->step, step)) {
            return -1;
        }
        if (*step == 0) {
            PyErr_SetString(PyExc_ValueError, "slice step cannot be zero");
            return -1;
        }
        if (*step < -PY_SSIZE_T_MAX) {
            *step = -PY_SSIZE_T_MAX;       /* keeps -step from overflowing */
        }
    }
    if (r->start == Py_None) {
        *start = *step < 0 ? PY_SSIZE_T_MAX : 0;
    }
    else if (!slice_index(r->start, start)) {
        return -1;
    }
    if (r->stop == Py_None) {
        *stop = *step < 0 ? PY_SSIZE_T_MIN : PY_SSIZE_T_MAX;
    }
    else if (!slice_index(r->stop, stop)) {
        return -1;
    }
    return 0;
}

Py_ssize_t
PySlice_AdjustIndices(Py_ssize_t length, Py_ssize_t *start, Py_ssize_t *stop, Py_ssize_t step)
{
    if (*start < 0) {
        *start += length;
        if (*start < 0) {
            *start = (step < 0) ? -1 : 0;
        }
    }
    else if (*start >= length) {
        *start = (step < 0) ? length - 1 : length;
    }
    if (*stop < 0) {
        *stop += length;
        if (*stop < 0) {
            *stop = (step < 0) ? -1 : 0;
        }
    }
    else if (*stop >= length) {
        *stop = (step < 0) ? length - 1 : length;
    }
    if (step < 0) {
        return *stop < *start ? (*start - *stop - 1) / (-step) + 1 : 0;
    }
    return *start < *stop ? (*stop - *start - 1) / step + 1 : 0;
}
