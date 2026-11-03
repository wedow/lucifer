/* The _lucifer primitives (DESIGN 4.4): C over layouts, called by kernel
   Python as ordinary functions. */
#ifndef LU_PRIM_H
#define LU_PRIM_H

#include "lucifer.h"

/* Per-interpreter state of the _lucifer module. */
struct lu_prim_state {
    PyInterpreterState *interp;         /* the interpreter the module belongs to */
};

PyObject *lu_prim_write(PyObject *module, PyObject *const *args, Py_ssize_t nargs);
PyObject *lu_prim_str_join(PyObject *module, PyObject *const *args, Py_ssize_t nargs);

#endif
