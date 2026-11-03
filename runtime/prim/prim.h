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
extern PyMethodDef lu_prim_type_methods[];      /* runtime/prim/type.c */
extern PyMethodDef lu_prim_descr_methods[];     /* runtime/object/descr.c */
extern PyMethodDef lu_prim_slot_methods[];      /* runtime/types/slots.c */
PyObject *lu_prim_str_join(PyObject *module, PyObject *const *args, Py_ssize_t nargs);

#endif
