/* bytes, bytearray and memoryview primitives (runtime/prim/bytes.c,
   memoryview.c) and the buffer protocol (runtime/capi/buffer.c). */
#ifndef LU_PRIM_BYTES_H
#define LU_PRIM_BYTES_H

#include "prim/prim.h"

/* The data of a bytes-like object: bytes in place, anything else through
   PyObject_GetBuffer(PyBUF_SIMPLE), released by lu_bv_put. */
typedef struct {
    const unsigned char *p;
    Py_ssize_t n;
    Py_buffer view;
    int held;
} lu_bv;

int lu_bv_get(PyObject *o, lu_bv *v);
void lu_bv_put(lu_bv *v);

/* ba[start:stop] = data for a bytes-like data, under ba's critical
   section: 0, or -1 with an error set. */
int lu_bytearray_splice(PyObject *ba, Py_ssize_t start, Py_ssize_t stop, PyObject *data);
/* bytearray.append of a byte value 0..255: 0, or -1 with an error set. */
int lu_bytearray_append(PyObject *ba, int byte);

void lu_bytes_init(void);
void lu_memoryview_init(void);
extern PyMethodDef lu_prim_bytes_methods[];     /* runtime/prim/bytes.c */
extern PyMethodDef lu_prim_mv_methods[];        /* runtime/prim/memoryview.c */

#endif
