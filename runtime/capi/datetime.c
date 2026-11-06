/* The datetime C API (Modules/_datetimemodule.c capsule): the types of the
   Python implementation and constructors that call them. Extensions use the
   constructors and the type checks; they never read the instances' fields.

   The capsule's types are static (a C API type must not be a heap type, and
   must outlive interpreters; Modules/_testcapi datetime.c asserts it). The
   Python implementation's classes are rebased onto them, so the type checks
   hold by inheritance, and a constructor handed one of them builds the
   class it stands for, the bases not being instantiable themselves. */

#include "prim/prim.h"
#include "datetime.h"

static PyObject *py_date, *py_datetime, *py_time, *py_delta, *py_tzinfo;

static PyObject *
constructible(PyObject *type)
{
    if (type == (PyObject *)&lu_date_base_type) {
        return py_date;
    }
    if (type == (PyObject *)&lu_datetime_base_type) {
        return py_datetime;
    }
    if (type == (PyObject *)&lu_time_base_type) {
        return py_time;
    }
    if (type == (PyObject *)&lu_timedelta_base_type) {
        return py_delta;
    }
    if (type == (PyObject *)&lu_tzinfo_base_type) {
        return py_tzinfo;
    }
    return type;
}

static PyObject *
construct(PyTypeObject *type, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    PyObject *args = Py_VaBuildValue(format, ap);
    va_end(ap);
    PyObject *r = args != NULL ? PyObject_CallObject(constructible((PyObject *)type), args) : NULL;
    Py_XDECREF(args);
    return r;
}

static PyObject *
date_from_date(int y, int m, int d, PyTypeObject *type)
{
    return construct(type, "(iii)", y, m, d);
}

static PyObject *
datetime_from_fields(int y, int mo, int d, int h, int mi, int s, int us, PyObject *tz, int fold,
                     PyTypeObject *type)
{
    PyObject *args = Py_BuildValue("(iiiiiiiO)", y, mo, d, h, mi, s, us, tz);
    PyObject *kw = fold ? Py_BuildValue("{s:i}", "fold", fold) : NULL;
    type = (PyTypeObject *)constructible((PyObject *)type);
    PyObject *r = args != NULL && (!fold || kw != NULL) ? PyObject_Call((PyObject *)type, args, kw) : NULL;
    Py_XDECREF(args);
    Py_XDECREF(kw);
    return r;
}

static PyObject *
datetime_from_date_and_time(int y, int mo, int d, int h, int mi, int s, int us, PyObject *tz,
                            PyTypeObject *type)
{
    return datetime_from_fields(y, mo, d, h, mi, s, us, tz, 0, type);
}

static PyObject *
time_from_time_and_fold(int h, int mi, int s, int us, PyObject *tz, int fold, PyTypeObject *type)
{
    PyObject *args = Py_BuildValue("(iiiiO)", h, mi, s, us, tz);
    PyObject *kw = fold ? Py_BuildValue("{s:i}", "fold", fold) : NULL;
    type = (PyTypeObject *)constructible((PyObject *)type);
    PyObject *r = args != NULL && (!fold || kw != NULL) ? PyObject_Call((PyObject *)type, args, kw) : NULL;
    Py_XDECREF(args);
    Py_XDECREF(kw);
    return r;
}

static PyObject *
time_from_time(int h, int mi, int s, int us, PyObject *tz, PyTypeObject *type)
{
    return time_from_time_and_fold(h, mi, s, us, tz, 0, type);
}

static PyObject *
delta_from_delta(int days, int seconds, int us, int normalize, PyTypeObject *type)
{
    return construct(type, "(iii)", days, seconds, us);
}

/* datetime.timezone(offset[, name]) */
static PyObject *
timezone_from_timezone(PyObject *offset, PyObject *name)
{
    PyObject *module = PyImport_ImportModule("datetime");
    PyObject *timezone = module != NULL ? PyObject_GetAttrString(module, "timezone") : NULL;
    Py_XDECREF(module);
    PyObject *r = timezone == NULL ? NULL
        : name != NULL ? PyObject_CallFunctionObjArgs(timezone, offset, name, NULL)
                       : PyObject_CallOneArg(timezone, offset);
    Py_XDECREF(timezone);
    return r;
}

/* type.fromtimestamp(*args, **kwargs) */
static PyObject *
from_timestamp(PyObject *type, PyObject *args, PyObject *kwargs)
{
    PyObject *f = PyObject_GetAttrString(constructible(type), "fromtimestamp");
    PyObject *r = f != NULL ? PyObject_Call(f, args, kwargs) : NULL;
    Py_XDECREF(f);
    return r;
}

static PyObject *
date_from_timestamp(PyObject *type, PyObject *args)
{
    return from_timestamp(type, args, NULL);
}

/* datetime_type_bases(): the static bases the Python implementation's
   classes are rebased onto, in capsule order. They take object's __new__
   (a static type with none of its own is marked uninstantiable, and these
   are as instantiable as object); the kernel has booted by the time this
   runs, from the datetime module. */
static PyObject *
prim_datetime_type_bases(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    if (nargs != 0) {
        lu_err_format(PyExc_TypeError, "datetime_type_bases() takes no arguments");
        return NULL;
    }
    PyTypeObject *const bases[] = {
        &lu_date_base_type, &lu_datetime_base_type, &lu_time_base_type,
        &lu_timedelta_base_type, &lu_tzinfo_base_type,
    };
    for (size_t i = 0; i < sizeof bases / sizeof bases[0]; i++) {
        bases[i]->tp_new = PyBaseObject_Type.tp_new;
        bases[i]->tp_flags &= ~Py_TPFLAGS_DISALLOW_INSTANTIATION;
    }
    PyObject *r = PyTuple_New(5);
    if (r != NULL) {
        PyTuple_SET_ITEM(r, 0, Py_NewRef((PyObject *)&lu_date_base_type));
        PyTuple_SET_ITEM(r, 1, Py_NewRef((PyObject *)&lu_datetime_base_type));
        PyTuple_SET_ITEM(r, 2, Py_NewRef((PyObject *)&lu_time_base_type));
        PyTuple_SET_ITEM(r, 3, Py_NewRef((PyObject *)&lu_timedelta_base_type));
        PyTuple_SET_ITEM(r, 4, Py_NewRef((PyObject *)&lu_tzinfo_base_type));
    }
    return r;
}

/* datetime_capi(date, datetime, time, timedelta, tzinfo, utc): the capsule
   datetime.datetime_CAPI for the Python implementation's classes. */
static PyObject *
prim_datetime_capi(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    if (nargs != 6) {
        lu_err_format(PyExc_TypeError, "datetime_capi(date, datetime, time, timedelta, tzinfo, utc)");
        return NULL;
    }
    PyDateTime_CAPI *api = PyMem_RawMalloc(sizeof *api);     /* lives as long as the process */
    if (api == NULL) {
        return PyErr_NoMemory();
    }
    Py_XDECREF(py_date);     /* a second call rebinds the classes */
    Py_XDECREF(py_datetime);
    Py_XDECREF(py_time);
    Py_XDECREF(py_delta);
    Py_XDECREF(py_tzinfo);
    py_date = Py_NewRef(args[0]);
    py_datetime = Py_NewRef(args[1]);
    py_time = Py_NewRef(args[2]);
    py_delta = Py_NewRef(args[3]);
    py_tzinfo = Py_NewRef(args[4]);
    *api = (PyDateTime_CAPI){
        &lu_date_base_type, &lu_datetime_base_type, &lu_time_base_type,
        &lu_timedelta_base_type, &lu_tzinfo_base_type, Py_NewRef(args[5]),
        date_from_date, datetime_from_date_and_time, time_from_time, delta_from_delta,
        timezone_from_timezone, (PyObject *(*)(PyObject *, PyObject *, PyObject *))from_timestamp,
        date_from_timestamp, datetime_from_fields, time_from_time_and_fold,
    };
    return PyCapsule_New(api, PyDateTime_CAPSULE_NAME, NULL);
}

PyMethodDef lu_prim_datetime_methods[] = {
    { "datetime_capi", (PyCFunction)(void (*)(void))prim_datetime_capi, METH_FASTCALL, NULL },
    { "datetime_type_bases", (PyCFunction)(void (*)(void))prim_datetime_type_bases, METH_FASTCALL, NULL },
    { NULL, NULL, 0, NULL },
};
