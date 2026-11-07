/* perf support (runtime/interp/perf.c): the perf map, the jitdump and the
   stack trampolines (FOUNDATIONS 2.12). */
#ifndef LU_INTERP_PERF_H
#define LU_INTERP_PERF_H

#include "lucifer.h"

enum lu_perf_backend { LU_PERF_NONE, LU_PERF_MAP, LU_PERF_JIT };

/* Installs the trampolines PyConfig.perf_profiling asks for (-X perf,
   -X perf_jit). 0, or -1 with an exception set. */
int lu_perf_start(PyInterpreterState *interp);

/* The trampolines' PEP 523 hook, under which compiled code still runs
   (and calls compiled callees directly: units have perf names of their own). */
PyObject *lu_perf_eval(PyThreadState *ts, struct _PyInterpreterFrame *f, int throwflag);

/* Names a compiled unit of `code`, entered at `pc`, in the active backend:
   nothing when the trampolines are off. */
void lu_perf_unit(const void *addr, size_t size, PyCodeObject *code, int pc);

extern PyMethodDef lu_sys_perf_methods[];

#endif
