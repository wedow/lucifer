/* Calendar time under the time port: the C library's gmtime_r, localtime_r,
   mktime, strftime and tzset, and clock_getres. The time module
   (lib/modules/time.py) keeps CPython's argument checks and struct_time. */

#define _GNU_SOURCE
#include "prim/prim.h"

#include <errno.h>
#include <string.h>
#include <time.h>

static PyObject *
os_error(void)
{
    PyObject *args[2] = { PyLong_FromLong(errno), PyUnicode_FromString(strerror(errno)) };
    PyObject *exc = args[0] != NULL && args[1] != NULL ? lu_call(PyExc_OSError, args, 2, NULL) : NULL;
    Py_XDECREF(args[0]);
    Py_XDECREF(args[1]);
    if (exc != NULL) {
        PyErr_SetObject((PyObject *)Py_TYPE(exc), exc);
        Py_DECREF(exc);
    }
    return NULL;
}

/* tm_tuple(seconds, local): (year, mon, mday, hour, min, sec, wday, yday,
   isdst, zone, gmtoff) with CPython's conventions (mon 1-12, wday Monday=0,
   yday 1-366). */
static PyObject *
prim_tm_tuple(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    long long secs;
    long hi;
    if (nargs != 2 || lu_long_as_long(args[0], &hi) < 0) {
        return NULL;
    }
    secs = hi;
    time_t t = (time_t)secs;
    struct tm tm;
    if ((args[1] == Py_True ? localtime_r(&t, &tm) : gmtime_r(&t, &tm)) == NULL) {
        return os_error();
    }
    long long year = (long long)tm.tm_year + 1900;
    long long f[9] = { year, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
                       (tm.tm_wday + 6) % 7, tm.tm_yday + 1, tm.tm_isdst };
    PyObject *t_ = PyTuple_New(11);
    for (int i = 0; t_ != NULL && i < 9; i++) {
        PyObject *n = PyLong_FromLongLong(f[i]);
        if (n == NULL) {
            Py_CLEAR(t_);
            break;
        }
        PyTuple_SET_ITEM(t_, i, n);
    }
    if (t_ != NULL) {
        PyObject *zone = PyUnicode_FromString(tm.tm_zone != NULL ? tm.tm_zone : "");
        PyObject *off = PyLong_FromLong((long)tm.tm_gmtoff);
        if (zone == NULL || off == NULL) {
            Py_XDECREF(zone);
            Py_XDECREF(off);
            Py_CLEAR(t_);
        }
        else {
            PyTuple_SET_ITEM(t_, 9, zone);
            PyTuple_SET_ITEM(t_, 10, off);
        }
    }
    return t_;
}

static void
fill_tm(struct tm *tm, long v[9])
{
    memset(tm, 0, sizeof *tm);
    tm->tm_year = (int)(v[0] - 1900);
    tm->tm_mon = (int)v[1] - 1;
    tm->tm_mday = (int)v[2];
    tm->tm_hour = (int)v[3];
    tm->tm_min = (int)v[4];
    tm->tm_sec = (int)v[5];
    tm->tm_wday = (int)((v[6] + 1) % 7);
    tm->tm_yday = (int)v[7] - 1;
    tm->tm_isdst = (int)v[8];
}

static int
fields_arg(PyObject *o, long v[9])
{
    if (!PyTuple_Check(o) || PyTuple_GET_SIZE(o) < 9) {
        lu_err_format(PyExc_TypeError, "a tuple of nine integers is required");
        return -1;
    }
    for (int i = 0; i < 9; i++) {
        if (lu_long_as_long(PyTuple_GET_ITEM(o, i), &v[i]) < 0) {
            return -1;
        }
    }
    return 0;
}

/* mktime(fields): seconds as an int, or None when the C library cannot say */
static PyObject *
prim_mktime(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    long v[9];
    struct tm tm;
    if (nargs != 1 || fields_arg(args[0], v) < 0) {
        return NULL;
    }
    fill_tm(&tm, v);
    tm.tm_wday = -1;
    time_t t = mktime(&tm);
    if (t == (time_t)-1 && tm.tm_wday == -1) {
        return Py_NewRef(Py_None);
    }
    return PyLong_FromLongLong((long long)t);
}

/* strftime(format, fields, zone, gmtoff) */
static PyObject *
prim_strftime(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    long v[9], gmtoff;
    struct tm tm;
    if (nargs != 4 || !PyUnicode_Check(args[0]) || fields_arg(args[1], v) < 0
        || lu_long_as_long(args[3], &gmtoff) < 0) {
        return PyErr_Occurred() ? NULL : (lu_err_format(PyExc_TypeError, "strftime(format, fields, zone, gmtoff)"), NULL);
    }
    const char *format = PyUnicode_AsUTF8(args[0]);
    const char *zone = args[2] == Py_None ? NULL : PyUnicode_AsUTF8(args[2]);
    if (format == NULL || (args[2] != Py_None && zone == NULL)) {
        return NULL;
    }
    fill_tm(&tm, v);
    tm.tm_gmtoff = gmtoff;
    tm.tm_zone = zone;
    size_t flen = strlen(format);
    if (flen == 0) {
        return PyUnicode_FromString("");
    }
    /* strftime answers 0 for an empty result and for a full buffer alike:
       grow until it fits, up to a bound no format can exceed. */
    for (size_t size = 1024;; size *= 2) {
        char *buf = PyMem_RawMalloc(size);
        if (buf == NULL) {
            return PyErr_NoMemory();
        }
        size_t n = strftime(buf, size, format, &tm);
        if (n > 0 || size > 256 * flen) {
            PyObject *result = PyUnicode_DecodeUTF8(buf, (Py_ssize_t)n, NULL);
            PyMem_RawFree(buf);
            return result;
        }
        PyMem_RawFree(buf);
    }
}

/* tzset() */
static PyObject *
prim_tzset(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    tzset();
    return Py_NewRef(Py_None);
}

/* clock_getres(clock_id): nanoseconds */
static PyObject *
prim_clock_getres(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    long clock;
    struct timespec ts;
    if (nargs != 1 || lu_long_as_long(args[0], &clock) < 0) {
        return NULL;
    }
    if (clock_getres((clockid_t)clock, &ts) < 0) {
        return os_error();
    }
    return PyLong_FromLongLong((long long)ts.tv_sec * 1000000000 + ts.tv_nsec);
}

#define FAST(name) { #name, (PyCFunction)(void (*)(void))prim_##name, METH_FASTCALL, NULL }
PyMethodDef lu_prim_calendar_methods[] = {
    FAST(tm_tuple), FAST(mktime), FAST(strftime), FAST(tzset), FAST(clock_getres),
    { NULL, NULL, 0, NULL },
};
