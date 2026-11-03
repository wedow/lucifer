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

/* Makes the handled exception `value`'s __context__ (PyErr_SetObject). */
void lu_exc_chain_context(PyObject *value);

/* The frame object of a generator's frame, which the frame keeps: made on
   first use, its position refreshed at each (gi_frame; the live frame
   object arrives with K1s). */
PyObject *lu_frame_object(struct lu_frame *f);

/* An exception nothing can take, reported as the default unraisable hook
   does: "Exception ignored while <what> <repr(obj)>:" and its traceback. */
void lu_err_unraisable(const char *what, PyObject *obj);
void lu_err_unraisable_text(const char *message, PyObject *obj);

/* The code object a traceback's frame ran. */
PyCodeObject *lu_frame_code(PyFrameObject *frame);

/* Prints an uncaught exception the way the default excepthook does. */
void lu_err_display(PyObject *exc);

#endif
