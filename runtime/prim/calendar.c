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
        if (errno == EOVERFLOW) {
            lu_err_format(PyExc_OverflowError, "timestamp out of range for platform time_t");
            return NULL;
        }
        return os_error();
    }
    long long year = (long long)tm.tm_year + 1900;
    PyObject *zone = PyUnicode_DecodeLocale(tm.tm_zone != NULL ? tm.tm_zone : "", "surrogateescape");
    if (zone == NULL) {
        return NULL;
    }
    PyObject *t_ = Py_BuildValue("(LiiiiiiiiOl)", year, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
                                 tm.tm_min, tm.tm_sec, (tm.tm_wday + 6) % 7, tm.tm_yday + 1,
                                 tm.tm_isdst, zone, (long)tm.tm_gmtoff);
    Py_DECREF(zone);
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
    PyObject *format = PyUnicode_EncodeLocale(args[0], "surrogateescape");
    PyObject *zone = args[2] == Py_None ? NULL : PyUnicode_EncodeLocale(args[2], "surrogateescape");
    if (format == NULL || (args[2] != Py_None && zone == NULL)) {
        Py_XDECREF(format);
        Py_XDECREF(zone);
        return NULL;
    }
    fill_tm(&tm, v);
    tm.tm_gmtoff = gmtoff;
    tm.tm_zone = zone != NULL ? PyBytes_AS_STRING(zone) : NULL;
    PyObject *result = NULL;
    if (PyBytes_GET_SIZE(format) == 0) {
        result = PyUnicode_FromString("");
    }
    for (size_t size = 1024; result == NULL; size *= 2) {
        char *buf = PyMem_RawMalloc(size);
        if (buf == NULL) {
            PyErr_NoMemory();
            break;
        }
        errno = 0;
        size_t n = strftime(buf, size, PyBytes_AS_STRING(format), &tm);
        if (n > 0 || (errno == 0 && size > 256 * PyBytes_GET_SIZE(format))) {
            result = PyUnicode_DecodeLocaleAndSize(buf, (Py_ssize_t)n, "surrogateescape");
            PyMem_RawFree(buf);
            break;
        }
        PyMem_RawFree(buf);
        if (size > 256 * (size_t)PyBytes_GET_SIZE(format) + 4096) {
            result = PyUnicode_FromString("");
        }
    }
    Py_DECREF(format);
    Py_XDECREF(zone);
    return result;
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
