/* PyStructSequence (Objects/structseq.c): the C-API over the kernel's
   structseq module, which builds the types and their instances once. An
   instance is a tuple of the fields in the sequence; the fields after those
   live in its dict. */

#include "interp/exceptions.h"
#include "lucifer.h"


/* structseq.NAME from the kernel (PyStructSequence_UnnamedField is in static.c). */
static PyObject *
kernel_function(const char *name)
{
    PyObject *lucifer = lu_ns_lookup(PyInterpreterState_Get()->modules, LU_ID(_lucifer));
    PyObject *key = PyUnicode_InternFromString("structseq");
    PyObject *module = key != NULL ? lu_ns_lookup(PyModule_GetDict(lucifer), key) : NULL;
    Py_XDECREF(key);
    key = module != NULL ? PyUnicode_InternFromString(name) : NULL;
    PyObject *f = key != NULL ? lu_ns_lookup(PyModule_GetDict(module), key) : NULL;
    Py_XDECREF(key);
    if (f == NULL && !PyErr_Occurred()) {
        lu_err_format(PyExc_SystemError, "the kernel has no structseq.%s", name);
    }
    return f;
}

PyTypeObject *
PyStructSequence_NewType(PyStructSequence_Desc *desc)
{
    Py_ssize_t n = 0;
    while (desc->fields[n].name != NULL) {
        n++;
    }
    PyObject *fields = PyTuple_New(n);
    for (Py_ssize_t i = 0; fields != NULL && i < n; i++) {
        const char *name = desc->fields[i].name;
        PyObject *field = name == PyStructSequence_UnnamedField ? Py_NewRef(Py_None)
                                                                 : PyUnicode_FromString(name);
        if (field == NULL) {
            Py_CLEAR(fields);
            break;
        }
        PyTuple_SET_ITEM(fields, i, field);
    }
    PyObject *f = fields != NULL ? kernel_function("structseq_type") : NULL;
    PyObject *args[4] = { PyUnicode_FromString(desc->name),
                          desc->doc != NULL ? PyUnicode_FromString(desc->doc) : Py_NewRef(Py_None),
                          fields, PyLong_FromLong(desc->n_in_sequence) };
    PyObject *type = f != NULL && args[0] != NULL && args[1] != NULL && args[3] != NULL
        ? lu_call(f, args, 4, NULL) : NULL;
    for (int i = 0; i < 4; i++) {
        Py_XDECREF(args[i]);
    }
    return (PyTypeObject *)type;
}

PyObject *
PyStructSequence_New(PyTypeObject *type)
{
    PyObject *f = kernel_function("structseq_blank");
    PyObject *arg = (PyObject *)type;
    return f != NULL ? lu_call(f, &arg, 1, NULL) : NULL;
}

void
PyStructSequence_SetItem(PyObject *op, Py_ssize_t i, PyObject *value)
{
    if (i < Py_SIZE(op)) {
        PyObject **slot = &((PyTupleObject *)op)->ob_item[i];
        PyObject *old = *slot;
        *slot = value;
        Py_XDECREF(old);
        return;
    }
    PyObject *f = kernel_function("structseq_set_hidden");
    PyObject *index = PyLong_FromLongLong(i);
    PyObject *args[3] = { op, index, value };
    PyObject *result = f != NULL && index != NULL ? lu_call(f, args, 3, NULL) : NULL;
    if (result == NULL) {
        PyErr_Clear();
    }
    Py_XDECREF(result);
    Py_XDECREF(index);
    Py_XDECREF(value);          /* SetItem steals the value */
}

PyObject *
PyStructSequence_GetItem(PyObject *op, Py_ssize_t i)
{
    return ((PyTupleObject *)op)->ob_item[i];
}
