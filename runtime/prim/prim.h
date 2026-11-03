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
extern PyMethodDef lu_prim_str_methods[];       /* runtime/prim/str.c */
extern PyMethodDef lu_prim_int_methods[];       /* runtime/prim/int.c */
extern PyMethodDef lu_sys_int_methods[];        /* sys's int functions, runtime/prim/int.c */
extern PyMethodDef lu_prim_exc_methods[];       /* runtime/interp/exceptions.c */
PyObject *lu_prim_str_join(PyObject *module, PyObject *const *args, Py_ssize_t nargs);
PyObject *lu_prim_text_repr(PyObject *module, PyObject *const *args, Py_ssize_t nargs);

void lu_striter_init(void);
PyObject *lu_str_join_tuple(PyObject *sep, PyObject *items);
PyObject *lu_str_concat(PyObject *a, PyObject *b);

/* Objects/unicodectype.c: the Unicode database's case mappings (not exported). */
int _PyUnicode_ToLowerFull(Py_UCS4 ch, Py_UCS4 *res);
int _PyUnicode_ToTitleFull(Py_UCS4 ch, Py_UCS4 *res);
int _PyUnicode_ToUpperFull(Py_UCS4 ch, Py_UCS4 *res);
int _PyUnicode_ToFoldedFull(Py_UCS4 ch, Py_UCS4 *res);
int _PyUnicode_IsCased(Py_UCS4 ch);
int _PyUnicode_IsCaseIgnorable(Py_UCS4 ch);
int _PyUnicode_IsXidStart(Py_UCS4 ch);
int _PyUnicode_IsXidContinue(Py_UCS4 ch);

#endif
