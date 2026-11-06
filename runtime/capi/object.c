/* Object initialization, calls and the attribute and item protocols from C
   (Objects/object.c, call.c, abstract.c). */

#include "interp/frame.h"

#include <stdarg.h>

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

/* Calls from C (Objects/call.c): vectorcall where the type has it, else
   tp_call with a tuple and a dict. */

LU_EXPORT PyObject *
_PyObject_MakeTpCall(PyThreadState *ts, PyObject *callable, PyObject *const *args,
                     Py_ssize_t nargs, PyObject *kwnames)
{
    ternaryfunc call = Py_TYPE(callable)->tp_call;
    if (call == NULL) {
        lu_err_format(PyExc_TypeError, "'%.200s' object is not callable", Py_TYPE(callable)->tp_name);
        return NULL;
    }
    PyObject *tuple = PyTuple_New(nargs);
    if (tuple == NULL) {
        return NULL;
    }
    for (Py_ssize_t i = 0; i < nargs; i++) {
        PyTuple_SET_ITEM(tuple, i, Py_NewRef(args[i]));
    }
    PyObject *kwargs = NULL;
    Py_ssize_t nkw = kwnames != NULL ? PyTuple_GET_SIZE(kwnames) : 0;
    if (nkw) {
        kwargs = PyDict_New();
        for (Py_ssize_t i = 0; kwargs != NULL && i < nkw; i++) {
            if (PyDict_SetItem(kwargs, PyTuple_GET_ITEM(kwnames, i), args[nargs + i]) < 0) {
                Py_CLEAR(kwargs);
            }
        }
        if (kwargs == NULL) {
            Py_DECREF(tuple);
            return NULL;
        }
    }
    PyObject *result = call(callable, tuple, kwargs);
    Py_DECREF(tuple);
    Py_XDECREF(kwargs);
    return result;
}

PyObject *
lu_call(PyObject *callable, PyObject *const *args, size_t nargsf, PyObject *kwnames)
{
    vectorcallfunc vector = PyVectorcall_Function(callable);
    if (vector != NULL) {
        return vector(callable, args, nargsf, kwnames);
    }
    return _PyObject_MakeTpCall(PyThreadState_Get(), callable, args, PyVectorcall_NARGS(nargsf),
                                kwnames);
}

PyObject *
PyObject_Vectorcall(PyObject *callable, PyObject *const *args, size_t nargsf, PyObject *kwnames)
{
    return lu_call(callable, args, nargsf, kwnames);
}

PyObject *
PyObject_CallOneArg(PyObject *callable, PyObject *arg)
{
    return lu_call(callable, &arg, 1, NULL);
}

PyObject *
PyObject_CallNoArgs(PyObject *callable)
{
    return lu_call(callable, NULL, 0, NULL);
}

PyObject *
PyObject_Call(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    vectorcallfunc vector = PyVectorcall_Function(callable);
    if (vector == NULL) {
        ternaryfunc call = Py_TYPE(callable)->tp_call;
        if (call == NULL) {
            lu_err_format(PyExc_TypeError, "'%.200s' object is not callable",
                          Py_TYPE(callable)->tp_name);
            return NULL;
        }
        return call(callable, args, kwargs);
    }
    Py_ssize_t nargs;
    PyObject *kwnames;
    PyObject **stack = lu_unpack_call(args, kwargs != NULL && ((PyDictObject *)kwargs)->ma_used
                                      ? kwargs : NULL, &nargs, &kwnames);
    if (stack == NULL) {
        return NULL;
    }
    PyObject *result = vector(callable, stack, nargs, kwnames);
    Py_ssize_t total = nargs + (kwnames != NULL ? PyTuple_GET_SIZE(kwnames) : 0);
    for (Py_ssize_t i = 0; i < total; i++) {
        Py_DECREF(stack[i]);
    }
    Py_XDECREF(kwnames);
    PyMem_Free(stack - 1);
    return result;
}

PyObject *
PyVectorcall_Call(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    return PyObject_Call(callable, args, kwargs);
}

PyObject *
PyObject_CallFunctionObjArgs(PyObject *callable, ...)
{
    PyObject *stack[16];
    Py_ssize_t n = 0;
    va_list ap;
    va_start(ap, callable);
    for (PyObject *arg; (arg = va_arg(ap, PyObject *)) != NULL; ) {
        if (n == 16) {
            va_end(ap);
            lu_err_format(PyExc_SystemError, "PyObject_CallFunctionObjArgs: more than 16 arguments");
            return NULL;
        }
        stack[n++] = arg;
    }
    va_end(ap);
    return lu_call(callable, stack, n, NULL);
}

/* The attribute and item protocols (Objects/object.c, abstract.c): the
   type's slot, which for kernel types and classes runs kernel Python. */

PyObject *
PyObject_GetAttr(PyObject *v, PyObject *name)
{
    if (!PyUnicode_Check(name)) {
        lu_err_format(PyExc_TypeError, "attribute name must be string, not '%.200s'",
                      Py_TYPE(name)->tp_name);
        return NULL;
    }
    getattrofunc get = Py_TYPE(v)->tp_getattro;
    if (get == NULL) {
        lu_err_format(PyExc_AttributeError, "'%.100s' object has no attribute '%s'",
                      Py_TYPE(v)->tp_name, PyUnicode_AsUTF8(name));
        return NULL;
    }
    return get(v, name);
}

PyObject *
PyObject_GetAttrString(PyObject *v, const char *name)
{
    if (v == NULL || name == NULL) {
        if (!PyErr_Occurred()) {
            PyErr_SetString(PyExc_SystemError, "null argument to internal routine");
        }
        return NULL;
    }
    PyObject *s = PyUnicode_FromString(name);
    if (s == NULL) {
        return NULL;
    }
    PyObject *r = PyObject_GetAttr(v, s);
    Py_DECREF(s);
    return r;
}

int
PyObject_SetAttr(PyObject *v, PyObject *name, PyObject *value)
{
    if (!PyUnicode_Check(name)) {
        lu_err_format(PyExc_TypeError, "attribute name must be string, not '%.200s'",
                      Py_TYPE(name)->tp_name);
        return -1;
    }
    setattrofunc set = Py_TYPE(v)->tp_setattro;
    if (set == NULL) {
        lu_err_format(PyExc_TypeError, "'%.100s' object has no attributes (%s .%s)",
                      Py_TYPE(v)->tp_name, value == NULL ? "del" : "assign to",
                      PyUnicode_AsUTF8(name));
        return -1;
    }
    return set(v, name, value);
}

int
PyObject_DelAttr(PyObject *v, PyObject *name)
{
    return PyObject_SetAttr(v, name, NULL);
}

int
PyObject_SetAttrString(PyObject *v, const char *name, PyObject *value)
{
    if (v == NULL || name == NULL) {
        if (!PyErr_Occurred()) {
            PyErr_SetString(PyExc_SystemError, "null argument to internal routine");
        }
        return -1;
    }
    PyObject *s = PyUnicode_FromString(name);
    if (s == NULL) {
        return -1;
    }
    int r = PyObject_SetAttr(v, s, value);
    Py_DECREF(s);
    return r;
}

PyObject *
PyObject_GenericGetAttr(PyObject *obj, PyObject *name)
{
    PyObject *args[2] = { obj, name };
    return lu_call_kernel(LU_K_generic_getattr, args, 2);
}

int
PyObject_GenericSetAttr(PyObject *obj, PyObject *name, PyObject *value)
{
    PyObject *args[3] = { obj, name, value != NULL ? value : LU_MISSING };
    PyObject *r = lu_call_kernel(LU_K_generic_setattr, args, 3);
    Py_XDECREF(r);
    return r == NULL ? -1 : 0;
}

PyObject *
PyObject_GetItem(PyObject *o, PyObject *key)
{
    if (o == NULL || key == NULL) {
        if (!PyErr_Occurred()) {
            PyErr_SetString(PyExc_SystemError, "null argument to internal routine");
        }
        return NULL;
    }
    PyMappingMethods *m = Py_TYPE(o)->tp_as_mapping;
    if (m != NULL && m->mp_subscript != NULL) {
        return m->mp_subscript(o, key);
    }
    PySequenceMethods *sq = Py_TYPE(o)->tp_as_sequence;
    if (sq != NULL && sq->sq_item != NULL) {
        if (!PyIndex_Check(key)) {
            lu_err_format(PyExc_TypeError, "sequence index must be integer, not '%.200s'",
                          Py_TYPE(key)->tp_name);
            return NULL;
        }
        Py_ssize_t i = PyNumber_AsSsize_t(key, PyExc_IndexError);
        return i == -1 && PyErr_Occurred() ? NULL : PySequence_GetItem(o, i);
    }
    if (PyType_Check(o)) {          /* list[int]: the class's __class_getitem__ */
        if (o == (PyObject *)&PyType_Type) {
            return Py_GenericAlias(o, key);
        }
        PyObject *method;
        if (PyObject_GetOptionalAttr(o, LU_ID(__class_getitem__), &method) < 0) {
            return NULL;
        }
        if (method != NULL) {
            PyObject *result = PyObject_CallOneArg(method, key);
            Py_DECREF(method);
            return result;
        }
        lu_err_format(PyExc_TypeError, "type '%.200s' is not subscriptable", ((PyTypeObject *)o)->tp_name);
        return NULL;
    }
    lu_err_format(PyExc_TypeError, "'%.200s' object is not subscriptable", Py_TYPE(o)->tp_name);
    return NULL;
}

/* mp_ass_subscript with a NULL value deletes. */
static int
ass_subscript(PyObject *o, PyObject *key, PyObject *v)
{
    PyMappingMethods *m = Py_TYPE(o)->tp_as_mapping;
    if (m != NULL && m->mp_ass_subscript != NULL) {
        return m->mp_ass_subscript(o, key, v);
    }
    lu_err_format(PyExc_TypeError, "'%.200s' object does not support item %s",
                  Py_TYPE(o)->tp_name, v == NULL ? "deletion" : "assignment");
    return -1;
}

int
PyObject_SetItem(PyObject *o, PyObject *key, PyObject *v)
{
    /* a NULL value is not a delete here: that is PyObject_DelItem's call
       (Objects/abstract.c PyObject_SetItem) */
    if (o == NULL || key == NULL || v == NULL) {
        if (!PyErr_Occurred()) {
            PyErr_SetString(PyExc_SystemError, "null argument to internal routine");
        }
        return -1;
    }
    return ass_subscript(o, key, v);
}

int
PyObject_DelItem(PyObject *o, PyObject *key)
{
    if (o == NULL || key == NULL) {
        if (!PyErr_Occurred()) {
            PyErr_SetString(PyExc_SystemError, "null argument to internal routine");
        }
        return -1;
    }
    return ass_subscript(o, key, NULL);
}

/* Allocation and initialization of objects that are not GC types
   (Objects/object.c _PyObject_New, _PyObject_NewVar). */

PyObject *
_PyObject_New(PyTypeObject *tp)
{
    return PyObject_Init(PyObject_Malloc(_PyObject_SIZE(tp)), tp);
}

PyVarObject *
PyObject_InitVar(PyVarObject *op, PyTypeObject *tp, Py_ssize_t size)
{
    if (op == NULL) {
        PyErr_NoMemory();
        return NULL;
    }
    /* The type first: Py_SET_SIZE's check reads it, and the memory a fresh
       block carries there is garbage (_PyObject_InitVar's order). */
    PyObject_Init((PyObject *)op, tp);
    Py_SET_SIZE(op, size);
    return op;
}

PyVarObject *
_PyObject_NewVar(PyTypeObject *tp, Py_ssize_t nitems)
{
    return PyObject_InitVar(PyObject_Malloc(_PyObject_VAR_SIZE(tp, nitems)), tp, nitems);
}

void
PyObject_ClearWeakRefs(PyObject *ob)
{
    lu_clear_weakrefs(ob);
}

/* The dict and inline values of a Py_TPFLAGS_MANAGED_DICT instance. */

void
PyObject_ClearManagedDict(PyObject *obj)
{
    lu_inline_clear(obj);
    if (Py_TYPE(obj)->tp_flags & Py_TPFLAGS_MANAGED_DICT) {
        Py_CLEAR(LU_MANAGED_DICT(obj));
    }
}

int
PyObject_VisitManagedDict(PyObject *obj, visitproc visit, void *arg)
{
    if (Py_TYPE(obj)->tp_flags & Py_TPFLAGS_MANAGED_DICT) {
        Py_VISIT(LU_MANAGED_DICT(obj));
    }
    return lu_inline_traverse(obj, visit, arg);
}

PyObject *
PyObject_SelfIter(PyObject *obj)
{
    return Py_NewRef(obj);
}

unsigned long
PyType_GetFlags(PyTypeObject *tp)
{
    return tp->tp_flags;
}

/* Attribute and call protocols. */

int
PyObject_GetOptionalAttr(PyObject *v, PyObject *name, PyObject **result)
{
    *result = PyObject_GetAttr(v, name);
    if (*result != NULL) {
        return 1;
    }
    if (!PyErr_ExceptionMatches(PyExc_AttributeError)) {
        return -1;
    }
    PyErr_Clear();
    return 0;
}

PyObject *
PyObject_VectorcallMethod(PyObject *name, PyObject *const *args, size_t nargsf, PyObject *kwnames)
{
    PyObject *method = PyObject_GetAttr(args[0], name);
    if (method == NULL) {
        return NULL;
    }
    PyObject *result = PyObject_Vectorcall(method, args + 1, (nargsf & ~PY_VECTORCALL_ARGUMENTS_OFFSET) - 1,
                                           kwnames);
    Py_DECREF(method);
    return result;
}

PyObject *
PyObject_VectorcallDict(PyObject *callable, PyObject *const *args, size_t nargsf, PyObject *kwargs)
{
    if (kwargs == NULL || PyDict_GET_SIZE(kwargs) == 0) {
        return PyObject_Vectorcall(callable, args, nargsf, NULL);
    }
    PyObject *tuple = PyTuple_New(PyVectorcall_NARGS(nargsf));
    for (Py_ssize_t i = 0; tuple != NULL && i < PyVectorcall_NARGS(nargsf); i++) {
        PyTuple_SET_ITEM(tuple, i, Py_NewRef(args[i]));
    }
    PyObject *result = tuple != NULL ? PyObject_Call(callable, tuple, kwargs) : NULL;
    Py_XDECREF(tuple);
    return result;
}

PyObject *
PyObject_CallMethodObjArgs(PyObject *obj, PyObject *name, ...)
{
    PyObject *stack[16], **args = stack;
    Py_ssize_t n = 1, cap = 16;
    va_list ap;
    stack[0] = obj;
    va_start(ap, name);
    for (PyObject *a; (a = va_arg(ap, PyObject *)) != NULL; ) {
        if (n == cap) {
            PyObject **bigger = PyMem_Malloc(2 * cap * sizeof(PyObject *));
            if (bigger == NULL) {
                va_end(ap);
                if (args != stack) {
                    PyMem_Free(args);
                }
                return PyErr_NoMemory();
            }
            memcpy(bigger, args, n * sizeof(PyObject *));
            if (args != stack) {
                PyMem_Free(args);
            }
            args = bigger;
            cap *= 2;
        }
        args[n++] = a;
    }
    va_end(ap);
    PyObject *result = PyObject_VectorcallMethod(name, args, n, NULL);
    if (args != stack) {
        PyMem_Free(args);
    }
    return result;
}

Py_ssize_t
PyObject_LengthHint(PyObject *o, Py_ssize_t defaultvalue)
{
    PyTypeObject *tp = Py_TYPE(o);
    if ((tp->tp_as_sequence != NULL && tp->tp_as_sequence->sq_length != NULL)
        || (tp->tp_as_mapping != NULL && tp->tp_as_mapping->mp_length != NULL)) {
        Py_ssize_t len = PyObject_Size(o);
        if (len >= 0) {
            return len;
        }
        if (!PyErr_ExceptionMatches(PyExc_TypeError)) {
            return -1;
        }
        PyErr_Clear();
    }
    PyObject *name = PyUnicode_InternFromString("__length_hint__");
    PyObject *hint = name != NULL ? _PyType_Lookup(tp, name) : NULL;
    if (hint == NULL) {
        Py_XDECREF(name);
        return PyErr_Occurred() ? -1 : defaultvalue;
    }
    PyObject *result = lu_call_special(o, name, NULL, 0);
    Py_DECREF(name);
    if (result == NULL) {
        if (!PyErr_ExceptionMatches(PyExc_TypeError)) {
            return -1;
        }
        PyErr_Clear();
        return defaultvalue;
    }
    if (result == Py_NotImplemented) {
        Py_DECREF(result);
        return defaultvalue;
    }
    if (!PyLong_Check(result)) {
        PyErr_Format(PyExc_TypeError, "__length_hint__ must be an integer, not %.100s",
                     Py_TYPE(result)->tp_name);
        Py_DECREF(result);
        return -1;
    }
    Py_ssize_t res = PyLong_AsSsize_t(result);
    Py_DECREF(result);
    if (res < 0 && PyErr_Occurred()) {
        return -1;
    }
    if (res < 0) {
        PyErr_Format(PyExc_ValueError, "__length_hint__() should return >= 0");
        return -1;
    }
    return res;
}

int
PyObject_AsFileDescriptor(PyObject *o)
{
    PyObject *meth = NULL, *fno = NULL;
    int fd;
    if (PyLong_Check(o)) {
        if (PyBool_Check(o)) {
            if (PyErr_WarnEx(PyExc_RuntimeWarning,
                    "bool is used as a file descriptor", 1))
            {
                return -1;
            }
        }
        fd = PyLong_AsInt(o);
    }
    else {
        meth = PyObject_GetAttrString(o, "fileno");
        if (meth == NULL) {
            if (PyErr_ExceptionMatches(PyExc_AttributeError)) {
                PyErr_SetString(PyExc_TypeError, "argument must be an int, or have a fileno() method.");
            }
            return -1;
        }
        fno = PyObject_CallNoArgs(meth);
        Py_DECREF(meth);
        if (fno == NULL) {
            return -1;
        }
        if (!PyLong_Check(fno)) {
            Py_DECREF(fno);
            PyErr_SetString(PyExc_TypeError, "fileno() returned a non-integer");
            return -1;
        }
        fd = PyLong_AsInt(fno);
        Py_DECREF(fno);
    }
    if (fd == -1 && PyErr_Occurred()) {
        return -1;
    }
    if (fd < 0) {
        PyErr_Format(PyExc_ValueError, "file descriptor cannot be a negative integer (%i)", fd);
        return -1;
    }
    return fd;
}

int
PyObject_Print(PyObject *op, FILE *fp, int flags)
{
    PyObject *s = op == NULL ? PyUnicode_FromString("<nil>")
        : (flags & Py_PRINT_RAW) ? PyObject_Str(op) : PyObject_Repr(op);
    const char *text = s != NULL ? PyUnicode_AsUTF8(s) : NULL;
    if (text == NULL) {
        Py_XDECREF(s);
        return -1;
    }
    fputs(text, fp);
    Py_DECREF(s);
    return ferror(fp) ? -1 : 0;
}

PyObject *
PySeqIter_New(PyObject *seq)
{
    if (!PySequence_Check(seq)) {
        PyErr_BadInternalCall();
        return NULL;
    }
    seqiterobject *it = (seqiterobject *)lu_object_new(&PySeqIter_Type, sizeof(seqiterobject));
    if (it != NULL) {
        it->it_seq = Py_NewRef(seq);
    }
    return (PyObject *)it;
}

PyObject *
Py_GenericAlias(PyObject *origin, PyObject *args)
{
    PyObject *a[2] = { origin, args };
    return lu_call_kernel(LU_K_generic_alias, a, 2);
}
