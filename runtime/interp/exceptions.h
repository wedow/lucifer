/* What the eval loop and the runtime need of exception objects and
   tracebacks (runtime/interp/exceptions.c). */
#ifndef LU_EXCEPTIONS_H
#define LU_EXCEPTIONS_H

#include "lucifer.h"

struct lu_frame;

/* The task's topmost handled exception record: the first of its exc_info
   chain whose exc_value is set. */
_PyErr_StackItem *lu_topmost_exc_info(PyThreadState *ts);

/* The exception in flight gains a traceback entry for `f`, whose raising
   instruction is code unit `lasti_unit`. -1 if the entry could not be made
   (the exception stays as it was). */
int lu_traceback_here(struct lu_frame *f, Py_ssize_t lasti_unit);

/* The co_exceptiontable entry covering code unit `index`: 1 with the stack
   depth to unwind to, the handler's unit and whether the raising
   instruction's offset is pushed (lasti). */
int lu_exception_handler(PyCodeObject *code, int index, int *level, int *handler, int *lasti);

/* The static exception classes whose construction is BaseException's own
   are called as CPython's BaseException_vectorcall makes them
   (runtime/capi/errors.c); once the kernel has filled their dicts. */
void lu_exc_vectorcalls(void);

/* RAISE_VARARGS as the kernel's do_raise: 1 with the exception set when
   exc and its cause (or MISSING) are exceptions or classes of them (called)
   or a None cause; 0 when the kernel must refuse them. */
int lu_raise_native(PyObject *exc, PyObject *cause);

/* Makes the handled exception `value`'s __context__ (PyErr_SetObject). */
void lu_exc_chain_context(PyObject *value);

/* An exception nothing can take, reported as the default unraisable hook
   does: "Exception ignored while <what> <repr(obj)>:" and its traceback. */
void lu_err_unraisable(const char *what, PyObject *obj);
void lu_err_unraisable_text(const char *message, PyObject *obj);
void lu_stderr_write(const char *text, size_t n);

/* Prints an uncaught exception the way the default excepthook does. */
void lu_err_display(PyObject *exc);

#endif
