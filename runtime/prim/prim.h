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
extern PyMethodDef lu_prim_float_methods[];     /* runtime/prim/float.c */
extern PyMethodDef lu_prim_complex_methods[];   /* runtime/prim/complex.c */
extern PyMethodDef lu_prim_math_methods[];      /* runtime/prim/math.c */
extern PyMethodDef lu_prim_calendar_methods[];  /* runtime/prim/calendar.c */
extern PyMethodDef lu_prim_datetime_methods[];  /* runtime/capi/datetime.c */
extern PyMethodDef lu_prim_zlib_methods[];      /* runtime/prim/zlib.c */
extern PyMethodDef lu_prim_passwd_methods[];    /* runtime/prim/passwd.c */
extern PyMethodDef lu_prim_ctypes_methods[];    /* runtime/prim/ctypes.c */
extern PyMethodDef lu_prim_mt_methods[];        /* runtime/prim/mt.c */
extern PyMethodDef lu_prim_faulthandler_methods[];  /* runtime/prim/faulthandler.c */
extern PyMethodDef lu_prim_ucd_methods[];       /* runtime/prim/ucd.c */
extern PyMethodDef lu_prim_compiler_methods[];  /* runtime/compiler/lu_cc.c */
extern PyMethodDef lu_sys_int_methods[];        /* sys's int functions, runtime/prim/int.c */
extern PyMethodDef lu_prim_exc_methods[];       /* runtime/interp/exceptions.c */
extern PyMethodDef lu_prim_storage_methods[];   /* runtime/prim/storage.c */
extern PyMethodDef lu_prim_dict_methods[];      /* runtime/prim/dict.c */
extern PyMethodDef lu_prim_set_methods[];       /* runtime/prim/set.c */
extern PyMethodDef lu_prim_gen_methods[];       /* runtime/interp/gen.c */
extern PyMethodDef lu_prim_frame_methods[];     /* runtime/interp/frame.c */
extern PyMethodDef lu_prim_builtins_methods[];  /* runtime/prim/builtins.c */
extern PyMethodDef lu_prim_os_methods[];        /* runtime/prim/os.c */
extern PyMethodDef lu_prim_thread_methods[];    /* runtime/prim/thread.c */
extern PyMethodDef lu_prim_gc_methods[];        /* runtime/interp/gc.c */
extern PyMethodDef lu_prim_signal_methods[];    /* runtime/interp/signals.c */
extern PyMethodDef lu_prim_weakref_methods[];   /* runtime/object/weakref.c */
extern PyMethodDef lu_prim_import_methods[];    /* runtime/interp/import.c */
extern PyMethodDef lu_prim_code_methods[];      /* runtime/interp/code.c */
extern PyMethodDef lu_sys_frame_methods[];      /* sys's recursion limit, runtime/interp/frame.c */
PyObject *lu_prim_str_join(PyObject *module, PyObject *const *args, Py_ssize_t nargs);
PyObject *lu_prim_text_repr(PyObject *module, PyObject *const *args, Py_ssize_t nargs);

/* float.c and complex.c */
int lu_as_double(PyObject *o, double *d);
PyObject *lu_float_richcompare(double i, PyObject *w, int op);
PyObject *lu_complex_pow(PyObject *v, PyObject *w);
char *lu_number_text(PyObject *s, Py_ssize_t *len);
Py_ssize_t lu_strip_underscores(char *s, Py_ssize_t len);

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
