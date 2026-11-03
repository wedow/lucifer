/* Runtime-internal declarations shared across runtime/. */
#ifndef LUCIFER_H
#define LUCIFER_H

#include "Python.h"

#define LU_EXPORT __attribute__((visibility("default")))
#define LU_STUB __attribute__((weak))
#define LU_UNIMPLEMENTED(name) lu_unimplemented(name)

/* Report an unported C-API function by name and abort. */
_Noreturn void lu_unimplemented(const char *name);

/* Biased reference counting (runtime/object/). */
void lu_brc_register_thread(void);
void lu_brc_queue_object(PyObject *ob);
Py_ssize_t lu_explicit_merge_refcount(PyObject *op, Py_ssize_t extra);

#endif
