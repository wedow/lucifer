/* The buffer protocol for C callers (Objects/abstract.c, memoryobject.c):
   the Py_buffer a type exports through bf_getbuffer, and the helpers around
   it. Strided copies handle C and Fortran order without suboffsets, which
   is what the exporters here (bytes, bytearray, memoryview) produce. */

#include "prim/bytes.h"

#include <string.h>

int
PyObject_CheckBuffer(PyObject *obj)
{
    PyBufferProcs *pb = Py_TYPE(obj)->tp_as_buffer;
    return pb != NULL && pb->bf_getbuffer != NULL;
}

int
PyObject_GetBuffer(PyObject *obj, Py_buffer *view, int flags)
{
    PyBufferProcs *pb = Py_TYPE(obj)->tp_as_buffer;
    if (pb == NULL || pb->bf_getbuffer == NULL) {
        lu_err_format(PyExc_TypeError, "a bytes-like object is required, not '%.100s'",
                      Py_TYPE(obj)->tp_name);
        return -1;
    }
    return pb->bf_getbuffer(obj, view, flags);
}

void
PyBuffer_Release(Py_buffer *view)
{
    PyObject *obj = view->obj;
    if (obj == NULL) {
        return;
    }
    PyBufferProcs *pb = Py_TYPE(obj)->tp_as_buffer;
    if (pb != NULL && pb->bf_releasebuffer != NULL) {
        pb->bf_releasebuffer(obj, view);
    }
    view->obj = NULL;
    Py_DECREF(obj);
}

int
PyBuffer_FillInfo(Py_buffer *view, PyObject *obj, void *buf, Py_ssize_t len, int readonly, int flags)
{
    if (view == NULL) {
        PyErr_SetString(PyExc_BufferError, "PyBuffer_FillInfo: view==NULL argument is obsolete");
        return -1;
    }
    if (flags != PyBUF_SIMPLE) {
        if (flags == PyBUF_READ || flags == PyBUF_WRITE) {
            PyErr_BadInternalCall();
            return -1;
        }
        if ((flags & PyBUF_WRITABLE) == PyBUF_WRITABLE && readonly == 1) {
            PyErr_SetString(PyExc_BufferError, "Object is not writable.");
            return -1;
        }
    }
    view->obj = Py_XNewRef(obj);
    view->buf = buf;
    view->len = len;
    view->readonly = readonly;
    view->itemsize = 1;
    view->format = (flags & PyBUF_FORMAT) == PyBUF_FORMAT ? "B" : NULL;
    view->ndim = 1;
    view->shape = (flags & PyBUF_ND) == PyBUF_ND ? &view->len : NULL;
    view->strides = (flags & PyBUF_STRIDES) == PyBUF_STRIDES ? &view->itemsize : NULL;
    view->suboffsets = NULL;
    view->internal = NULL;
    return 0;
}

void
PyBuffer_FillContiguousStrides(int nd, Py_ssize_t *shape, Py_ssize_t *strides, int itemsize, char order)
{
    Py_ssize_t sd = itemsize;
    if (order == 'F') {
        for (int k = 0; k < nd; k++) {
            strides[k] = sd;
            sd *= shape[k] ? shape[k] : 1;
        }
    }
    else {
        for (int k = nd - 1; k >= 0; k--) {
            strides[k] = sd;
            sd *= shape[k] ? shape[k] : 1;
        }
    }
}

int
PyBuffer_IsContiguous(const Py_buffer *view, char order)
{
    if (view->suboffsets != NULL) {
        return 0;
    }
    if (view->len == 0) {
        return 1;
    }
    if (view->strides == NULL) {
        return order == 'C' || order == 'F' || order == 'A';
    }
    if (order == 'A') {
        return PyBuffer_IsContiguous(view, 'C') || PyBuffer_IsContiguous(view, 'F');
    }
    Py_ssize_t sd = view->itemsize;
    for (int k = 0; k < view->ndim; k++) {
        int i = order == 'F' ? k : view->ndim - 1 - k;
        Py_ssize_t dim = view->shape[i];
        if (dim > 1 && view->strides[i] != sd) {
            return 0;
        }
        sd *= dim;
    }
    return 1;
}

void *
PyBuffer_GetPointer(const Py_buffer *view, const Py_ssize_t *indices)
{
    char *pointer = (char *)view->buf;
    for (int i = 0; i < view->ndim; i++) {
        pointer += view->strides[i] * indices[i];
        if (view->suboffsets != NULL && view->suboffsets[i] >= 0) {
            pointer = *((char **)pointer) + view->suboffsets[i];
        }
    }
    return pointer;
}

/* Copies between a strided view and a contiguous buffer: to_buf says which way. */
static void
copy_strided(char *buf, const Py_buffer *view, int to_buf, char order)
{
    Py_ssize_t idx[64] = { 0 };
    Py_ssize_t items = view->itemsize ? view->len / view->itemsize : 0;
    for (Py_ssize_t n = 0; n < items; n++) {
        char *p = (char *)view->buf;
        for (int d = 0; d < view->ndim; d++) {
            p += idx[d] * view->strides[d];
        }
        if (to_buf) {
            memcpy(buf + n * view->itemsize, p, view->itemsize);
        }
        else {
            memcpy(p, buf + n * view->itemsize, view->itemsize);
        }
        if (order == 'F') {
            for (int d = 0; d < view->ndim; d++) {
                if (++idx[d] < view->shape[d]) break;
                idx[d] = 0;
            }
        }
        else {
            for (int d = view->ndim - 1; d >= 0; d--) {
                if (++idx[d] < view->shape[d]) break;
                idx[d] = 0;
            }
        }
    }
}

int
PyBuffer_ToContiguous(void *buf, const Py_buffer *view, Py_ssize_t len, char order)
{
    if (len > view->len) {
        len = view->len;
    }
    if (PyBuffer_IsContiguous(view, order)) {
        memcpy(buf, view->buf, len);
        return 0;
    }
    if (len != view->len) {
        PyErr_SetString(PyExc_ValueError, "PyBuffer_ToContiguous: len != view->len");
        return -1;
    }
    if (view->suboffsets != NULL) {
        PyErr_SetString(PyExc_NotImplementedError, "buffers with suboffsets are not supported");
        return -1;
    }
    copy_strided(buf, view, 1, order == 'F' ? 'F' : 'C');
    return 0;
}

int
PyBuffer_FromContiguous(const Py_buffer *view, const void *buf, Py_ssize_t len, char order)
{
    if (len > view->len) {
        len = view->len;
    }
    if (PyBuffer_IsContiguous(view, order)) {
        memcpy(view->buf, buf, len);
        return 0;
    }
    if (view->suboffsets != NULL) {
        PyErr_SetString(PyExc_NotImplementedError, "buffers with suboffsets are not supported");
        return -1;
    }
    copy_strided((char *)buf, view, 0, order == 'F' ? 'F' : 'C');
    return 0;
}
