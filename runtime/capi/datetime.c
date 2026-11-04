/* The datetime C API (Modules/_datetimemodule.c capsule): the types of the
   Python implementation and constructors that call them. Extensions use the
   constructors and the type checks; they never read the instances' fields. */

#include "prim/prim.h"
#include "datetime.h"

static PyObject *
construct(PyTypeObject *type, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    PyObject *args = Py_VaBuildValue(format, ap);
    va_end(ap);
    PyObject *r = args != NULL ? PyObject_CallObject((PyObject *)type, args) : NULL;
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
    PyObject *f = PyObject_GetAttrString(type, "fromtimestamp");
    PyObject *r = f != NULL ? PyObject_Call(f, args, kwargs) : NULL;
    Py_XDECREF(f);
    return r;
}

static PyObject *
date_from_timestamp(PyObject *type, PyObject *args)
{
    return from_timestamp(type, args, NULL);
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
    *api = (PyDateTime_CAPI){
        (PyTypeObject *)Py_NewRef(args[0]), (PyTypeObject *)Py_NewRef(args[1]),
        (PyTypeObject *)Py_NewRef(args[2]), (PyTypeObject *)Py_NewRef(args[3]),
        (PyTypeObject *)Py_NewRef(args[4]), Py_NewRef(args[5]),
        date_from_date, datetime_from_date_and_time, time_from_time, delta_from_delta,
        timezone_from_timezone, (PyObject *(*)(PyObject *, PyObject *, PyObject *))from_timestamp,
        date_from_timestamp, datetime_from_fields, time_from_time_and_fold,
    };
    return PyCapsule_New(api, PyDateTime_CAPSULE_NAME, NULL);
}

PyMethodDef lu_prim_datetime_methods[] = {
    { "datetime_capi", (PyCFunction)(void (*)(void))prim_datetime_capi, METH_FASTCALL, NULL },
    { NULL, NULL, 0, NULL },
};
