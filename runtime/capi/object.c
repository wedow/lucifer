/* Object initialization (Objects/object.c). */

#include "lucifer.h"

PyObject *
PyObject_Init(PyObject *op, PyTypeObject *tp)
{
    if (op == NULL) {
        return PyErr_NoMemory();
    }
    Py_SET_TYPE(op, tp);
    if (tp->tp_flags & Py_TPFLAGS_HEAPTYPE) {
        Py_INCREF(tp);
    }
    _Py_NewReference(op);
    return op;
}
