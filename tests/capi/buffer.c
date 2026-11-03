/* The buffer protocol as C extensions use it: Py_buffer exports of bytes,
   bytearray and memoryview, the BufferError a resize raises while one is
   held, and the C functions over the bytes and bytearray layouts. Preloaded
   into the interpreter, which runs the checks, prints "capi buffer ok" and
   exits. */

#include "Python.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int failures;

#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "buffer.c:%d: check failed: %s\n", __LINE__, #cond); \
    failures++; } } while (0)

static void
check_bytes(void)
{
    PyObject *b = PyBytes_FromStringAndSize("hello", 5);
    char *s;
    Py_ssize_t n;
    CHECK(PyBytes_AsStringAndSize(b, &s, &n) == 0 && n == 5 && !memcmp(s, "hello", 5));
    CHECK(PyBytes_Size(b) == 5 && !strcmp(PyBytes_AsString(b), "hello"));
    CHECK(PyObject_CheckBuffer(b));
    Py_buffer view;
    CHECK(PyObject_GetBuffer(b, &view, PyBUF_SIMPLE) == 0);
    CHECK(view.buf == PyBytes_AS_STRING(b) && view.len == 5 && view.readonly == 1 && view.obj == b);
    PyBuffer_Release(&view);
    CHECK(view.obj == NULL);
    CHECK(PyObject_GetBuffer(b, &view, PyBUF_WRITABLE) < 0 && PyErr_ExceptionMatches(PyExc_BufferError));
    PyErr_Clear();
    CHECK(PyObject_GetBuffer(b, &view, PyBUF_FULL_RO) == 0);
    CHECK(view.ndim == 1 && view.itemsize == 1 && !strcmp(view.format, "B") && view.shape[0] == 5 && view.strides[0] == 1);
    PyBuffer_Release(&view);
    PyObject *w = PyBytes_FromString(" world");
    PyBytes_Concat(&b, w);
    CHECK(b != NULL && PyBytes_GET_SIZE(b) == 11 && !memcmp(PyBytes_AS_STRING(b), "hello world", 11));
    Py_DECREF(w);
    Py_DECREF(b);
    CHECK(!PyObject_CheckBuffer(Py_None));
    CHECK(PyObject_GetBuffer(Py_None, &view, PyBUF_SIMPLE) < 0 && PyErr_ExceptionMatches(PyExc_TypeError));
    PyErr_Clear();
}

static void
check_bytearray(void)
{
    PyObject *a = PyByteArray_FromStringAndSize("abc", 3);
    CHECK(PyByteArray_Size(a) == 3 && !memcmp(PyByteArray_AsString(a), "abc", 3));
    Py_buffer view;
    CHECK(PyObject_GetBuffer(a, &view, PyBUF_WRITABLE) == 0 && !view.readonly);
    ((char *)view.buf)[0] = 'X';
    /* While the export is held the bytearray may change but not resize. */
    CHECK(PyByteArray_Resize(a, 10) < 0 && PyErr_ExceptionMatches(PyExc_BufferError));
    PyErr_Clear();
    CHECK(PyByteArray_Resize(a, 3) == 0);
    PyBuffer_Release(&view);
    CHECK(PyByteArray_Resize(a, 10) == 0 && PyByteArray_Size(a) == 10);
    CHECK(PyByteArray_AsString(a)[0] == 'X' && PyByteArray_AsString(a)[10] == '\0');
    CHECK(PyByteArray_Resize(a, 2) == 0 && PyByteArray_Size(a) == 2);
    Py_DECREF(a);
}

static void
check_memoryview(void)
{
    PyObject *a = PyByteArray_FromStringAndSize("abcdef", 6);
    PyObject *m = PyMemoryView_FromObject(a);
    CHECK(m != NULL && PyMemoryView_Check(m));
    Py_buffer *base = PyMemoryView_GET_BUFFER(m);
    CHECK(base->len == 6 && base->itemsize == 1 && !base->readonly && PyMemoryView_GET_BASE(m) == a);
    /* The memoryview holds the bytearray's buffer. */
    CHECK(PyByteArray_Resize(a, 8) < 0 && PyErr_ExceptionMatches(PyExc_BufferError));
    PyErr_Clear();
    Py_buffer view;
    CHECK(PyObject_GetBuffer(m, &view, PyBUF_FULL) == 0 && view.len == 6 && view.buf == base->buf);
    PyBuffer_Release(&view);
    Py_DECREF(m);
    CHECK(PyByteArray_Resize(a, 8) == 0);
    Py_DECREF(a);

    char mem[4] = { 1, 2, 3, 4 };
    PyObject *fm = PyMemoryView_FromMemory(mem, 4, PyBUF_WRITE);
    CHECK(fm != NULL && PyMemoryView_GET_BUFFER(fm)->buf == mem && !PyMemoryView_GET_BUFFER(fm)->readonly);
    Py_DECREF(fm);
    fm = PyMemoryView_FromMemory(mem, 4, PyBUF_READ);
    CHECK(PyMemoryView_GET_BUFFER(fm)->readonly);
    Py_DECREF(fm);

    Py_buffer info;
    CHECK(PyBuffer_FillInfo(&info, NULL, mem, 4, 1, PyBUF_FULL_RO) == 0);
    PyObject *fb = PyMemoryView_FromBuffer(&info);
    CHECK(fb != NULL && PyMemoryView_GET_BUFFER(fb)->len == 4 && PyMemoryView_GET_BUFFER(fb)->readonly);
    Py_DECREF(fb);
}

static void
check_helpers(void)
{
    Py_ssize_t shape[2] = { 2, 3 }, strides[2];
    PyBuffer_FillContiguousStrides(2, shape, strides, 4, 'C');
    CHECK(strides[0] == 12 && strides[1] == 4);
    PyBuffer_FillContiguousStrides(2, shape, strides, 4, 'F');
    CHECK(strides[0] == 4 && strides[1] == 8);
    char data[6] = { 1, 2, 3, 4, 5, 6 };
    Py_buffer v = { .buf = data, .len = 6, .itemsize = 1, .ndim = 2, .shape = shape,
                    .strides = (Py_ssize_t[]){ 3, 1 }, .format = "B" };
    CHECK(PyBuffer_IsContiguous(&v, 'C') && !PyBuffer_IsContiguous(&v, 'F'));
    char out[6];
    CHECK(PyBuffer_ToContiguous(out, &v, 6, 'F') == 0);
    CHECK(out[0] == 1 && out[1] == 4 && out[2] == 2 && out[3] == 5 && out[4] == 3 && out[5] == 6);
    Py_ssize_t idx[2] = { 1, 2 };
    CHECK(*(char *)PyBuffer_GetPointer(&v, idx) == 6);
}

__attribute__((constructor)) static void
check(void)
{
    Py_InitializeEx(0);
    check_bytes();
    check_bytearray();
    check_memoryview();
    check_helpers();
    if (failures == 0) {
        puts("capi buffer ok");
    }
    fflush(stdout);
    _exit(failures != 0);
}
