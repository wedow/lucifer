/* Code objects (runtime/interp/code.c). */
#ifndef LU_CODE_H
#define LU_CODE_H

#include "lucifer.h"

/* Local kinds in co_localspluskinds (Include/internal/pycore_code.h). */
#define CO_FAST_HIDDEN  0x10
#define CO_FAST_LOCAL   0x20
#define CO_FAST_CELL    0x40
#define CO_FAST_FREE    0x80

/* co_flags bit of code frozen into the kernel: its frames are kernel-hidden.
   CPython leaves the bit unused. */
#define LU_CO_KERNEL 0x40000000

/* co_flags bit of the functions of a builtin module port: their frames are
   hidden as a C module's are, but classes stay ordinary classes. */
#define LU_CO_HIDDEN 0x20000000

/* The fields marshal reads (_PyCodeConstructor), borrowed. */
struct lu_code_fields {
    PyObject *filename, *name, *qualname;
    int flags;
    PyObject *code;
    int firstlineno;
    PyObject *linetable;
    PyObject *consts, *names;
    PyObject *localsplusnames, *localspluskinds;
    int argcount, posonlyargcount, kwonlyargcount;
    int stacksize;
    PyObject *exceptiontable;
};

PyObject *lu_code_new(const struct lu_code_fields *fields);

/* co_code: the bytecode with specialized opcodes made generic and caches cleared. */
PyObject *lu_code_bytes(PyCodeObject *co);

#endif
