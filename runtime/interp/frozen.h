/* The frozen kernel (build/frozen.c, written by tools/freeze). */
#ifndef LU_FROZEN_H
#define LU_FROZEN_H

#include "lucifer.h"

struct lu_frozen {
    const char *name;                   /* the module the file implements */
    const unsigned char *data;          /* marshal of its code object */
    int size;
    const char *orig;                   /* the module it is a copy of, or NULL (frozen table) */
    int package;                        /* a frozen package */
};

/* NULL-terminated. The kernel is in boot order; builtin is lib/modules, the
   builtin modules; frozen is what importlib's FrozenImporter finds. */
extern const struct lu_frozen lu_frozen_kernel[];
extern const struct lu_frozen lu_frozen_builtin[];
extern const struct lu_frozen lu_frozen_modules[];

void lu_hide_frames(PyObject *code);

/* Runs every frozen kernel module. Returns -1 with an exception set. */
int lu_kernel_boot(PyInterpreterState *interp);

#endif
