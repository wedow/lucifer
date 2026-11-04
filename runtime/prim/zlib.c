/* zlib streams: the _lucifer primitives lib/modules/zlib.py is built on, a
   thin adapter over the system zlib. A stream is a capsule around a z_stream;
   every call that touches one holds the capsule's critical section. The
   primitives report zlib's status and message instead of raising, so the
   module raises its own error class. */

#include "prim/prim.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define STREAM_NAME "lucifer.zlib.stream"

typedef struct {
    z_stream z;
    int deflating;
} stream;

static void
stream_destroy(PyObject *capsule)
{
    stream *s = PyCapsule_GetPointer(capsule, STREAM_NAME);
    if (s != NULL) {
        s->deflating ? deflateEnd(&s->z) : inflateEnd(&s->z);
        free(s);
    }
}

static stream *
stream_of(PyObject *capsule)
{
    return PyCapsule_GetPointer(capsule, STREAM_NAME);
}

/* The bytes of a bytes-like argument, held until `release`. */
static int
get_bytes(PyObject *o, Py_buffer *view)
{
    return PyObject_GetBuffer(o, view, PyBUF_SIMPLE);
}

static PyObject *
status_result(PyObject *out, int status, const z_stream *z)
{
    PyObject *msg = z->msg != NULL && status != Z_STREAM_ERROR ? PyUnicode_FromString(z->msg) : Py_NewRef(Py_None);
    PyObject *r = msg != NULL && out != NULL ? Py_BuildValue("(NiN)", out, status, msg) : NULL;
    if (r == NULL) {
        Py_XDECREF(out);
        Py_XDECREF(msg);
    }
    return r;
}

/* deflate_init(level, method, wbits, memlevel, strategy, zdict_or_None): (stream, status) */
static PyObject *
prim_zlib_deflate_init(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    int level, method, wbits, memlevel, strategy;
    if (nargs != 6 || !PyArg_Parse(args[0], "i", &level) || !PyArg_Parse(args[1], "i", &method)
        || !PyArg_Parse(args[2], "i", &wbits) || !PyArg_Parse(args[3], "i", &memlevel)
        || !PyArg_Parse(args[4], "i", &strategy)) {
        return PyErr_Occurred() ? NULL : PyErr_Format(PyExc_TypeError, "zlib_deflate_init(6 arguments)");
    }
    stream *s = calloc(1, sizeof *s);
    if (s == NULL) {
        return PyErr_NoMemory();
    }
    s->deflating = 1;
    int status = deflateInit2(&s->z, level, method, wbits, memlevel, strategy);
    if (status == Z_OK && args[5] != Py_None) {
        Py_buffer dict;
        if (get_bytes(args[5], &dict) < 0) {
            deflateEnd(&s->z);
            free(s);
            return NULL;
        }
        status = deflateSetDictionary(&s->z, dict.buf, (uInt)dict.len);
        PyBuffer_Release(&dict);
    }
    if (status != Z_OK) {
        PyObject *r = Py_BuildValue("(Oi)", Py_None, status);
        free(s);
        return r;
    }
    PyObject *cap = PyCapsule_New(s, STREAM_NAME, stream_destroy);
    return cap == NULL ? NULL : Py_BuildValue("(Ni)", cap, status);
}

/* inflate_init(wbits, zdict_or_None): (stream, status) */
static PyObject *
prim_zlib_inflate_init(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    int wbits;
    if (nargs != 2 || !PyArg_Parse(args[0], "i", &wbits)) {
        return PyErr_Occurred() ? NULL : PyErr_Format(PyExc_TypeError, "zlib_inflate_init(2 arguments)");
    }
    stream *s = calloc(1, sizeof *s);
    if (s == NULL) {
        return PyErr_NoMemory();
    }
    int status = inflateInit2(&s->z, wbits);
    if (status == Z_OK && args[1] != Py_None && wbits < 0) {
        Py_buffer dict;                     /* a raw stream takes its dictionary up front */
        if (get_bytes(args[1], &dict) < 0) {
            inflateEnd(&s->z);
            free(s);
            return NULL;
        }
        status = inflateSetDictionary(&s->z, dict.buf, (uInt)dict.len);
        PyBuffer_Release(&dict);
    }
    if (status != Z_OK) {
        PyObject *r = Py_BuildValue("(Oi)", Py_None, status);
        free(s);
        return r;
    }
    PyObject *cap = PyCapsule_New(s, STREAM_NAME, stream_destroy);
    return cap == NULL ? NULL : Py_BuildValue("(Ni)", cap, status);
}

/* Output that grows: room() makes at least `want` bytes free, or fails. */
struct output {
    char *p;
    size_t used, cap;
};

static int
room(struct output *o, size_t want, size_t limit)
{
    if (o->cap - o->used >= want) {
        return 0;
    }
    size_t cap = o->cap * 2 > o->used + want ? o->cap * 2 : o->used + want;
    if (limit != 0 && cap > limit) {
        cap = limit;
    }
    if (cap <= o->used) {
        return -1;
    }
    char *bigger = realloc(o->p, cap);
    if (bigger == NULL) {
        return -1;
    }
    o->p = bigger;
    o->cap = cap;
    return 0;
}

static uInt
clamp(size_t n)
{
    return n > UINT_MAX ? UINT_MAX : (uInt)n;
}

/* deflate(stream, data, flush): (output, status, message). Consumes all of data;
   the last chunk carries the flush mode. */
static PyObject *
prim_zlib_deflate(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    int flush;
    Py_buffer in;
    stream *s = nargs == 3 ? stream_of(args[0]) : NULL;
    if (s == NULL || !PyArg_Parse(args[2], "i", &flush) || get_bytes(args[1], &in) < 0) {
        return PyErr_Occurred() ? NULL : PyErr_Format(PyExc_TypeError, "zlib_deflate(stream, data, flush)");
    }
    PyObject *result = NULL;
    Py_BEGIN_CRITICAL_SECTION(args[0]);
    struct output o = { malloc(in.len / 2 + 64), 0, in.len / 2 + 64 };
    size_t remaining = (size_t)in.len;
    int status = Z_OK;
    while (o.p != NULL) {
        if (room(&o, 64, 0) < 0) {
            free(o.p);
            o.p = NULL;
            break;
        }
        uInt chunk = clamp(remaining);
        s->z.next_in = (Bytef *)in.buf + (in.len - remaining);
        s->z.avail_in = chunk;
        s->z.next_out = (Bytef *)o.p + o.used;
        s->z.avail_out = clamp(o.cap - o.used);
        uInt before_out = s->z.avail_out;
        status = deflate(&s->z, chunk == remaining ? flush : Z_NO_FLUSH);
        o.used += before_out - s->z.avail_out;
        remaining -= chunk - s->z.avail_in;
        if (status == Z_STREAM_ERROR || (s->z.avail_out != 0 && remaining == 0)) {
            break;
        }
    }
    if (o.p == NULL) {
        PyErr_NoMemory();
    }
    else {
        result = status_result(PyBytes_FromStringAndSize(o.p, (Py_ssize_t)o.used), status, &s->z);
        free(o.p);
    }
    Py_END_CRITICAL_SECTION();
    PyBuffer_Release(&in);
    return result;
}

/* inflate(stream, data, max_length, flush): (output, status, message, unread, needs_dict).
   max_length 0 is no limit. */
static PyObject *
prim_zlib_inflate(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    Py_ssize_t max_length;
    int flush;
    Py_buffer in;
    stream *s = nargs == 4 ? stream_of(args[0]) : NULL;
    if (s == NULL || !PyArg_Parse(args[2], "n", &max_length) || !PyArg_Parse(args[3], "i", &flush)
        || get_bytes(args[1], &in) < 0) {
        return PyErr_Occurred() ? NULL : PyErr_Format(PyExc_TypeError, "zlib_inflate(stream, data, max_length, flush)");
    }
    PyObject *result = NULL;
    Py_BEGIN_CRITICAL_SECTION(args[0]);
    size_t limit = max_length > 0 ? (size_t)max_length : 0;
    size_t first = limit != 0 && limit < in.len * 4 + 64 ? limit : in.len * 4 + 64;
    struct output o = { malloc(first), 0, first };
    size_t remaining = (size_t)in.len;
    int status = Z_OK;
    while (o.p != NULL) {
        if (limit != 0 && o.used >= limit) {
            break;
        }
        if (room(&o, 64, limit) < 0) {
            free(o.p);
            o.p = NULL;
            break;
        }
        uInt chunk = clamp(remaining);
        s->z.next_in = (Bytef *)in.buf + (in.len - remaining);
        s->z.avail_in = chunk;
        s->z.next_out = (Bytef *)o.p + o.used;
        s->z.avail_out = clamp(o.cap - o.used);
        uInt before_out = s->z.avail_out;
        status = inflate(&s->z, flush);
        o.used += before_out - s->z.avail_out;
        remaining -= chunk - s->z.avail_in;
        if (status == Z_STREAM_END || status == Z_NEED_DICT || (status < 0 && status != Z_BUF_ERROR)
            || s->z.avail_out != 0) {
            break;                           /* done, stuck, or the input is spent */
        }
    }
    if (o.p == NULL) {
        PyErr_NoMemory();
    }
    else {
        PyObject *msg = s->z.msg != NULL && status != Z_STREAM_ERROR ? PyUnicode_FromString(s->z.msg) : Py_NewRef(Py_None);
        PyObject *bytes = PyBytes_FromStringAndSize(o.p, (Py_ssize_t)o.used);
        free(o.p);
        result = msg != NULL && bytes != NULL
            ? Py_BuildValue("(OiOnO)", bytes, status, msg, (Py_ssize_t)remaining,
                            status == Z_NEED_DICT ? Py_True : Py_False)
            : NULL;
        Py_XDECREF(msg);
        Py_XDECREF(bytes);
    }
    Py_END_CRITICAL_SECTION();
    PyBuffer_Release(&in);
    return result;
}

/* set_dictionary(stream, zdict): the status, for a stream that asked for one. */
static PyObject *
prim_zlib_set_dictionary(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    Py_buffer dict;
    stream *s = nargs == 2 ? stream_of(args[0]) : NULL;
    if (s == NULL || get_bytes(args[1], &dict) < 0) {
        return PyErr_Occurred() ? NULL : PyErr_Format(PyExc_TypeError, "zlib_set_dictionary(stream, zdict)");
    }
    int status;
    Py_BEGIN_CRITICAL_SECTION(args[0]);
    status = s->deflating ? deflateSetDictionary(&s->z, dict.buf, (uInt)dict.len)
                          : inflateSetDictionary(&s->z, dict.buf, (uInt)dict.len);
    Py_END_CRITICAL_SECTION();
    PyBuffer_Release(&dict);
    return PyLong_FromLong(status);
}

/* copy(stream): (new stream, status) */
static PyObject *
prim_zlib_copy(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    stream *s = nargs == 1 ? stream_of(args[0]) : NULL;
    if (s == NULL) {
        return PyErr_Occurred() ? NULL : PyErr_Format(PyExc_TypeError, "zlib_copy(stream)");
    }
    stream *t = calloc(1, sizeof *t);
    if (t == NULL) {
        return PyErr_NoMemory();
    }
    t->deflating = s->deflating;
    int status;
    Py_BEGIN_CRITICAL_SECTION(args[0]);
    status = s->deflating ? deflateCopy(&t->z, &s->z) : inflateCopy(&t->z, &s->z);
    Py_END_CRITICAL_SECTION();
    if (status != Z_OK) {
        free(t);
        return Py_BuildValue("(Oi)", Py_None, status);
    }
    PyObject *cap = PyCapsule_New(t, STREAM_NAME, stream_destroy);
    return cap == NULL ? NULL : Py_BuildValue("(Ni)", cap, status);
}

/* crc32(data, value), adler32(data, value) */
static PyObject *
checksum(PyObject *const *args, Py_ssize_t nargs, int adler)
{
    unsigned long value;
    Py_buffer in;
    if (nargs != 2 || get_bytes(args[0], &in) < 0) {
        return PyErr_Occurred() ? NULL : PyErr_Format(PyExc_TypeError, "checksum(data, value)");
    }
    value = PyLong_AsUnsignedLongMask(args[1]);
    const Bytef *p = in.buf;
    for (size_t left = (size_t)in.len; left > 0; ) {
        uInt n = left > UINT_MAX ? UINT_MAX : (uInt)left;
        value = adler ? adler32(value, p, n) : crc32(value, p, n);
        p += n;
        left -= n;
    }
    PyBuffer_Release(&in);
    return PyLong_FromUnsignedLong(value);
}

static PyObject *
prim_zlib_crc32(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    return checksum(args, nargs, 0);
}

static PyObject *
prim_zlib_adler32(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    return checksum(args, nargs, 1);
}

static PyObject *
prim_zlib_version(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    return Py_BuildValue("(ss)", ZLIB_VERSION, zlibVersion());
}

#define FAST(name) { #name, (PyCFunction)(void (*)(void))prim_##name, METH_FASTCALL, NULL }
PyMethodDef lu_prim_zlib_methods[] = {
    FAST(zlib_deflate_init), FAST(zlib_inflate_init), FAST(zlib_deflate), FAST(zlib_inflate),
    FAST(zlib_set_dictionary), FAST(zlib_copy), FAST(zlib_crc32), FAST(zlib_adler32), FAST(zlib_version),
    { NULL, NULL, 0, NULL },
};
