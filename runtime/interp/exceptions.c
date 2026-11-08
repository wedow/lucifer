/* Exception objects, the handled-exception state, tracebacks and the frame
   records they hold (Objects/exceptions.c layouts, Python/errors.c,
   Python/traceback.c, Python/ceval.c get_exception_handler). The classes'
   behaviour is kernel Python (lib/kernel/exceptions.py); here are the
   layouts, their attributes and everything the interpreter loop reaches
   without calling Python. The exception in flight lives in the task's
   current_exception and the handled ones in its exc_info chain
   (FOUNDATIONS 2.8). */

#include "interp/frame.h"
#include "interp/code.h"
#include "interp/exceptions.h"
#include "opcode_ids.h"
#include "frameobject.h"
#include "prim/prim.h"

#include <stddef.h>
#include <string.h>

extern PyTypeObject *const lu_exception_types[];

/* Layouts: the fields that follow PyException_HEAD in the types that have
   any, as pointer-sized words; bit i of `ssize` marks word i as a
   Py_ssize_t. A subtype has the layout of its root. */
struct exc_layout {
    PyObject **root;
    size_t nwords;
    unsigned ssize;
};

static const struct exc_layout layouts[] = {
    { &PyExc_BaseExceptionGroup, 3, 0 },        /* msg, excs, excs_str */
    { &PyExc_SyntaxError, 9, 0 },
    { &PyExc_ImportError, 4, 0 },               /* msg, name, path, name_from */
    { &PyExc_UnicodeError, 5, 1 << 2 | 1 << 3 },/* encoding, object, start, end, reason */
    { &PyExc_SystemExit, 1, 0 },
    { &PyExc_OSError, 5, 1 << 4 },              /* errno, strerror, filename, filename2, written */
    { &PyExc_StopIteration, 1, 0 },
    { &PyExc_NameError, 1, 0 },
    { &PyExc_AttributeError, 2, 0 },            /* obj, name */
};

static const struct exc_layout *
layout_of(PyTypeObject *tp)
{
    for (size_t i = 0; i < sizeof layouts / sizeof layouts[0]; i++) {
        if (PyType_IsSubtype(tp, (PyTypeObject *)*layouts[i].root)) {
            return &layouts[i];
        }
    }
    return NULL;
}

static PyObject **
words_of(PyObject *op)
{
    return (PyObject **)((PyBaseExceptionObject *)op + 1);
}

static int
exc_traverse(PyObject *op, visitproc visit, void *arg)
{
    PyBaseExceptionObject *e = (PyBaseExceptionObject *)op;
    Py_VISIT(e->dict);
    Py_VISIT(e->args);
    Py_VISIT(e->notes);
    Py_VISIT(e->traceback);
    Py_VISIT(e->context);
    Py_VISIT(e->cause);
    const struct exc_layout *l = layout_of(Py_TYPE(op));
    for (size_t i = 0; l != NULL && i < l->nwords; i++) {
        if (!(l->ssize >> i & 1)) {
            Py_VISIT(words_of(op)[i]);
        }
    }
    return 0;
}

static int
exc_clear(PyObject *op)
{
    PyBaseExceptionObject *e = (PyBaseExceptionObject *)op;
    Py_CLEAR(e->dict);
    Py_CLEAR(e->args);
    Py_CLEAR(e->notes);
    Py_CLEAR(e->traceback);
    Py_CLEAR(e->cause);
    Py_CLEAR(e->context);
    const struct exc_layout *l = layout_of(Py_TYPE(op));
    for (size_t i = 0; l != NULL && i < l->nwords; i++) {
        if (!(l->ssize >> i & 1)) {
            Py_CLEAR(words_of(op)[i]);
        }
    }
    return 0;
}

static void
exc_dealloc(PyObject *op)
{
    PyBaseExceptionObject *e = (PyBaseExceptionObject *)op;
    Py_XDECREF(e->dict);
    Py_XDECREF(e->args);
    Py_XDECREF(e->notes);
    Py_XDECREF(e->traceback);
    Py_XDECREF(e->context);
    Py_XDECREF(e->cause);
    const struct exc_layout *l = layout_of(Py_TYPE(op));
    for (size_t i = 0; l != NULL && i < l->nwords; i++) {
        if (!(l->ssize >> i & 1)) {
            Py_XDECREF(words_of(op)[i]);
        }
    }
    lu_object_free(op);
}

/* BaseException's attributes (Objects/exceptions.c BaseException_getset). */

static PyObject *
exc_get_args(PyObject *op, void *closure)
{
    PyObject *args = ((PyBaseExceptionObject *)op)->args;
    return args != NULL ? Py_NewRef(args) : PyTuple_New(0);
}

static int
exc_set_args(PyObject *op, PyObject *value, void *closure)
{
    if (value == NULL) {
        lu_err_format(PyExc_TypeError, "args may not be deleted");
        return -1;
    }
    PyObject *args;
    if (PyTuple_CheckExact(value)) {
        args = Py_NewRef(value);
    }
    else if (PyList_CheckExact(value)) {
        args = lu_args_as_tuple(Py_None, value);
    }
    else {
        lu_err_format(PyExc_TypeError, "'%.200s' object is not iterable", Py_TYPE(value)->tp_name);
        return -1;
    }
    if (args == NULL) {
        return -1;
    }
    Py_XSETREF(((PyBaseExceptionObject *)op)->args, args);
    return 0;
}

static PyObject *
exc_get_traceback(PyObject *op, void *closure)
{
    return Py_NewRef(((PyBaseExceptionObject *)op)->traceback ?: Py_None);
}

static int
exc_set_traceback(PyObject *op, PyObject *value, void *closure)
{
    if (value == NULL) {
        lu_err_format(PyExc_TypeError, "__traceback__ may not be deleted");
        return -1;
    }
    return PyException_SetTraceback(op, value);
}

static PyObject *
exc_get_context(PyObject *op, void *closure)
{
    return Py_NewRef(((PyBaseExceptionObject *)op)->context ?: Py_None);
}

/* __context__ and __cause__ hold None or an exception. */
static int
exc_set_link(PyObject *value, PyObject **field, const char *name, const char *message)
{
    if (value == NULL) {
        lu_err_format(PyExc_TypeError, "%s may not be deleted", name);
        return -1;
    }
    if (value != Py_None && !PyExceptionInstance_Check(value)) {
        lu_err_format(PyExc_TypeError, "%s", message);
        return -1;
    }
    PyObject *old = *field;
    *field = value == Py_None ? NULL : Py_NewRef(value);
    Py_XDECREF(old);
    return 0;
}

static int
exc_set_context(PyObject *op, PyObject *value, void *closure)
{
    return exc_set_link(value, &((PyBaseExceptionObject *)op)->context, "__context__",
                        "exception context must be None or derive from BaseException");
}

static PyObject *
exc_get_cause(PyObject *op, void *closure)
{
    return Py_NewRef(((PyBaseExceptionObject *)op)->cause ?: Py_None);
}

static int
exc_set_cause(PyObject *op, PyObject *value, void *closure)
{
    PyBaseExceptionObject *e = (PyBaseExceptionObject *)op;
    if (exc_set_link(value, &e->cause, "__cause__",
                     "exception cause must be None or derive from BaseException") < 0) {
        return -1;
    }
    e->suppress_context = 1;
    return 0;
}

static PyGetSetDef base_getset[] = {
    { "__dict__", PyObject_GenericGetDict, PyObject_GenericSetDict, NULL, NULL },
    { "args", exc_get_args, exc_set_args, NULL, NULL },
    { "__traceback__", exc_get_traceback, exc_set_traceback, NULL, NULL },
    { "__context__", exc_get_context, exc_set_context, NULL, NULL },
    { "__cause__", exc_get_cause, exc_set_cause, NULL, NULL },
    { NULL },
};

static PyMemberDef base_members[] = {
    { "__suppress_context__", Py_T_BOOL, offsetof(PyBaseExceptionObject, suppress_context), 0, NULL },
    { NULL },
};

/* The other types' attributes: a word of the layout, by offset. */
#define WORD(type, field) offsetof(type, field), 0, NULL
#define RWORD(type, field) offsetof(type, field), Py_READONLY, NULL

static PyMemberDef system_exit_members[] = {
    { "code", _Py_T_OBJECT, WORD(PySystemExitObject, code) },
    { NULL },
};
static PyMemberDef stop_iteration_members[] = {
    { "value", _Py_T_OBJECT, WORD(PyStopIterationObject, value) },
    { NULL },
};
static PyMemberDef name_error_members[] = {
    { "name", _Py_T_OBJECT, WORD(PyNameErrorObject, name) },
    { NULL },
};
static PyMemberDef attribute_error_members[] = {
    { "name", _Py_T_OBJECT, WORD(PyAttributeErrorObject, name) },
    { "obj", _Py_T_OBJECT, WORD(PyAttributeErrorObject, obj) },
    { NULL },
};
static PyMemberDef import_error_members[] = {
    { "msg", _Py_T_OBJECT, WORD(PyImportErrorObject, msg) },
    { "name", _Py_T_OBJECT, WORD(PyImportErrorObject, name) },
    { "path", _Py_T_OBJECT, WORD(PyImportErrorObject, path) },
    { "name_from", _Py_T_OBJECT, WORD(PyImportErrorObject, name_from) },
    { NULL },
};
static PyMemberDef syntax_error_members[] = {
    { "msg", _Py_T_OBJECT, WORD(PySyntaxErrorObject, msg) },
    { "filename", _Py_T_OBJECT, WORD(PySyntaxErrorObject, filename) },
    { "lineno", _Py_T_OBJECT, WORD(PySyntaxErrorObject, lineno) },
    { "offset", _Py_T_OBJECT, WORD(PySyntaxErrorObject, offset) },
    { "text", _Py_T_OBJECT, WORD(PySyntaxErrorObject, text) },
    { "end_lineno", _Py_T_OBJECT, WORD(PySyntaxErrorObject, end_lineno) },
    { "end_offset", _Py_T_OBJECT, WORD(PySyntaxErrorObject, end_offset) },
    { "print_file_and_line", _Py_T_OBJECT, WORD(PySyntaxErrorObject, print_file_and_line) },
    { "_metadata", _Py_T_OBJECT, WORD(PySyntaxErrorObject, metadata) },
    { NULL },
};
static PyMemberDef unicode_error_members[] = {
    { "encoding", _Py_T_OBJECT, WORD(PyUnicodeErrorObject, encoding) },
    { "object", _Py_T_OBJECT, WORD(PyUnicodeErrorObject, object) },
    { "start", Py_T_PYSSIZET, WORD(PyUnicodeErrorObject, start) },
    { "end", Py_T_PYSSIZET, WORD(PyUnicodeErrorObject, end) },
    { "reason", _Py_T_OBJECT, WORD(PyUnicodeErrorObject, reason) },
    { NULL },
};
static PyMemberDef os_error_members[] = {
    { "errno", _Py_T_OBJECT, WORD(PyOSErrorObject, myerrno) },
    { "strerror", _Py_T_OBJECT, WORD(PyOSErrorObject, strerror) },
    { "filename", _Py_T_OBJECT, WORD(PyOSErrorObject, filename) },
    { "filename2", _Py_T_OBJECT, WORD(PyOSErrorObject, filename2) },
    { NULL },
};
static PyMemberDef group_members[] = {
    { "message", _Py_T_OBJECT, RWORD(PyBaseExceptionGroupObject, msg) },
    { "exceptions", _Py_T_OBJECT, RWORD(PyBaseExceptionGroupObject, excs) },
    { NULL },
};

/* OSError.characters_written: -1 means unset. */
static PyObject *
oserror_get_written(PyObject *op, void *closure)
{
    Py_ssize_t written = ((PyOSErrorObject *)op)->written;
    if (written == -1) {
        lu_err_format(PyExc_AttributeError, "characters_written");
        return NULL;
    }
    return PyLong_FromSsize_t(written);
}

static int
oserror_set_written(PyObject *op, PyObject *value, void *closure)
{
    long n;
    if (value == NULL) {
        if (((PyOSErrorObject *)op)->written == -1) {
            lu_err_format(PyExc_AttributeError, "characters_written");
            return -1;
        }
        ((PyOSErrorObject *)op)->written = -1;
        return 0;
    }
    if (!PyLong_Check(value) || lu_long_as_long(value, &n) < 0) {
        lu_err_format(PyExc_TypeError, "characters_written value must be an integer");
        return -1;
    }
    ((PyOSErrorObject *)op)->written = n;
    return 0;
}

static PyGetSetDef os_error_getset[] = {
    { "characters_written", oserror_get_written, oserror_set_written, NULL, NULL },
    { NULL },
};

static PyObject *
oserror_alloc(PyTypeObject *tp, Py_ssize_t nitems)
{
    PyObject *op = PyType_GenericAlloc(tp, nitems);
    if (op != NULL) {
        ((PyOSErrorObject *)op)->written = -1;
    }
    return op;
}

/* traceback (Python/traceback.c) */

/* A traceback of 100,000 entries dies in a loop, not a recursion: an entry
   held only by the one before it is freed here (CPython's trashcan). */
static void
tb_dealloc(PyObject *op)
{
    while (op != NULL) {
        PyTracebackObject *tb = (PyTracebackObject *)op;
        PyObject *next = (PyObject *)tb->tb_next;
        Py_XDECREF(tb->tb_frame);
        lu_object_free(op);
        op = NULL;
        if (next == NULL) {
            break;
        }
        if (lu_release_last(next)) {
            op = next;
        }
    }
}

static int
tb_traverse(PyObject *op, visitproc visit, void *arg)
{
    PyTracebackObject *tb = (PyTracebackObject *)op;
    Py_VISIT(tb->tb_next);
    Py_VISIT(tb->tb_frame);
    return 0;
}

static int
tb_clear(PyObject *op)
{
    PyTracebackObject *tb = (PyTracebackObject *)op;
    Py_CLEAR(tb->tb_next);
    Py_CLEAR(tb->tb_frame);
    return 0;
}

static PyObject *
tb_get_next(PyObject *op, void *closure)
{
    return Py_NewRef(((PyTracebackObject *)op)->tb_next ?: (PyTracebackObject *)Py_None);
}

static int
tb_set_next(PyObject *op, PyObject *value, void *closure)
{
    if (value == NULL) {
        lu_err_format(PyExc_TypeError, "can't delete tb_next attribute");
        return -1;
    }
    if (value != Py_None && !PyTraceBack_Check(value)) {
        lu_err_format(PyExc_TypeError, "expected traceback object, got '%s'",
                      Py_TYPE(value)->tp_name);
        return -1;
    }
    for (PyObject *c = value; c != Py_None; c = (PyObject *)((PyTracebackObject *)c)->tb_next) {
        if (c == op) {
            lu_err_format(PyExc_ValueError, "traceback loop detected");
            return -1;
        }
        if (((PyTracebackObject *)c)->tb_next == NULL) {
            break;
        }
    }
    PyTracebackObject *old = ((PyTracebackObject *)op)->tb_next;
    ((PyTracebackObject *)op)->tb_next = value == Py_None ? NULL : (PyTracebackObject *)Py_NewRef(value);
    Py_XDECREF(old);
    return 0;
}

static PyObject *
tb_get_lineno(PyObject *op, void *closure)
{
    PyTracebackObject *tb = (PyTracebackObject *)op;
    int line = tb->tb_lineno;
    if (line == -1) {
        line = PyCode_Addr2Line(lu_frame_code(tb->tb_frame), tb->tb_lasti);
    }
    return line < 0 ? Py_NewRef(Py_None) : PyLong_FromLong(line);
}

static PyMemberDef tb_members[] = {
    { "tb_frame", _Py_T_OBJECT, RWORD(PyTracebackObject, tb_frame) },
    { "tb_lasti", Py_T_INT, RWORD(PyTracebackObject, tb_lasti) },
    { NULL },
};

static PyGetSetDef tb_getset[] = {
    { "tb_next", tb_get_next, tb_set_next, NULL, NULL },
    { "tb_lineno", tb_get_lineno, NULL, NULL, NULL },
    { NULL },
};

/* A traceback entry for `frame` (a new reference taken), at byte offset `lasti`. */
static PyTracebackObject *
tb_new(PyObject *frame, PyTracebackObject *next, int lasti)
{
    PyTracebackObject *tb = (PyTracebackObject *)lu_object_new(&PyTraceBack_Type,
                                                               sizeof(PyTracebackObject));
    if (tb != NULL) {
        tb->tb_next = (PyTracebackObject *)Py_XNewRef((PyObject *)next);
        tb->tb_frame = (PyFrameObject *)Py_NewRef(frame);
        tb->tb_lasti = lasti;
        tb->tb_lineno = -1;
    }
    return tb;
}

/* PyTraceBack_Here: the unwinding frame joins the exception's traceback. */
int
lu_traceback_here(struct lu_frame *lf, Py_ssize_t lasti_unit)
{
    /* The exception is set aside while the entry is made, so that running
       out of memory leaves it as it was. */
    PyBaseExceptionObject *exc = (PyBaseExceptionObject *)PyErr_GetRaisedException();
    PyObject *fr = lu_frame_object(lf);
    PyTracebackObject *tb = fr != NULL
        ? tb_new(fr, (PyTracebackObject *)exc->traceback, (int)(lasti_unit * sizeof(lu_codeunit)))
        : NULL;
    Py_XDECREF(fr);
    if (tb == NULL) {
        PyErr_Clear();
    }
    else {
        Py_XDECREF(exc->traceback);
        exc->traceback = (PyObject *)tb;
    }
    PyErr_SetRaisedException((PyObject *)exc);
    return tb == NULL ? -1 : 0;
}

int
PyTraceBack_Here(PyFrameObject *frame)
{
    PyBaseExceptionObject *exc = (PyBaseExceptionObject *)PyThreadState_Get()->current_exception;
    PyTracebackObject *tb = tb_new((PyObject *)frame, (PyTracebackObject *)exc->traceback,
                                   PyFrame_GetLasti(frame));
    if (tb == NULL) {
        return -1;
    }
    Py_XDECREF(exc->traceback);
    exc->traceback = (PyObject *)tb;
    return 0;
}

/* A traceback entry for C code, FUNCNAME at FILENAME:LINENO, on the current
   exception (Python/traceback.c). */
LU_EXPORT void
_PyTraceback_Add(const char *funcname, const char *filename, int lineno)
{
    PyObject *exc = PyErr_GetRaisedException();
    PyObject *globals = PyDict_New();
    PyCodeObject *code = globals == NULL ? NULL : PyCode_NewEmpty(filename, funcname, lineno);
    PyFrameObject *frame = code == NULL ? NULL : PyFrame_New(PyThreadState_Get(), code, globals, NULL);
    Py_XDECREF(globals);
    Py_XDECREF(code);
    if (frame == NULL) {
        _PyErr_ChainExceptions1(exc);
        return;
    }
    PyErr_SetRaisedException(exc);
    PyTraceBack_Here(frame);
    Py_DECREF(frame);
}

/* Exception links (Objects/exceptions.c PyException_*). */

PyObject *
PyException_GetTraceback(PyObject *self)
{
    return Py_XNewRef(((PyBaseExceptionObject *)self)->traceback);
}

int
PyException_SetTraceback(PyObject *self, PyObject *tb)
{
    if (tb != Py_None && !PyTraceBack_Check(tb)) {
        lu_err_format(PyExc_TypeError, "__traceback__ must be a traceback or None");
        return -1;
    }
    PyObject *old = ((PyBaseExceptionObject *)self)->traceback;
    ((PyBaseExceptionObject *)self)->traceback = tb == Py_None ? NULL : Py_NewRef(tb);
    Py_XDECREF(old);
    return 0;
}

PyObject *
PyException_GetContext(PyObject *self)
{
    return Py_XNewRef(((PyBaseExceptionObject *)self)->context);
}

/* Steals a reference to context. */
void
PyException_SetContext(PyObject *self, PyObject *context)
{
    Py_XSETREF(((PyBaseExceptionObject *)self)->context, context);
}

PyObject *
PyException_GetCause(PyObject *self)
{
    return Py_XNewRef(((PyBaseExceptionObject *)self)->cause);
}

/* Steals a reference to cause. */
void
PyException_SetCause(PyObject *self, PyObject *cause)
{
    PyBaseExceptionObject *e = (PyBaseExceptionObject *)self;
    e->suppress_context = 1;
    Py_XSETREF(e->cause, cause);
}

PyObject *
PyException_GetArgs(PyObject *self)
{
    return exc_get_args(self, NULL);
}

void
PyException_SetArgs(PyObject *self, PyObject *args)
{
    Py_XSETREF(((PyBaseExceptionObject *)self)->args, Py_NewRef(args));
}

/* The UnicodeError attributes (Objects/exceptions.c PyUnicode*Error_*): the
   five words of the layout. Any UnicodeError subclass passes the type check,
   as in CPython. */

static int
check_unicode_error_type(PyObject *self, const char *expect_type)
{
    if (!PyObject_TypeCheck(self, (PyTypeObject *)PyExc_UnicodeError)) {
        lu_err_format(PyExc_TypeError, "expecting a %s object, got %s", expect_type,
                      Py_TYPE(self)->tp_name);
        return -1;
    }
    return 0;
}

/* A UnicodeError attribute that must be a str, or bytes when as_bytes. */
static int
check_unicode_error_attr(PyObject *attr, const char *name, int as_bytes)
{
    if (attr == NULL) {
        lu_err_format(PyExc_TypeError, "UnicodeError '%s' attribute is not set", name);
        return -1;
    }
    if (!(as_bytes ? PyBytes_Check(attr) : PyUnicode_Check(attr))) {
        lu_err_format(PyExc_TypeError, "UnicodeError '%s' attribute must be a %s",
                      name, as_bytes ? "bytes" : "string");
        return -1;
    }
    return 0;
}

static PyObject *
unicode_error_attr(PyObject *attr, const char *name, int as_bytes)
{
    return check_unicode_error_attr(attr, name, as_bytes) < 0 ? NULL : Py_NewRef(attr);
}

/* The length of the 'object' attribute, checked (bytes when decoding). */
static int
unicode_error_object_len(PyObject *self, Py_ssize_t *objlen, int as_bytes)
{
    PyObject *object = ((PyUnicodeErrorObject *)self)->object;
    if (check_unicode_error_attr(object, "object", as_bytes) < 0) {
        return -1;
    }
    *objlen = as_bytes ? PyBytes_GET_SIZE(object) : PyUnicode_GET_LENGTH(object);
    return 0;
}

/* The 'start' clipped to [0, max(0, objlen - 1)], the 'end' to
   [min(1, objlen), max(min(1, objlen), objlen)]. */
static Py_ssize_t
unicode_error_clip_start(Py_ssize_t start, Py_ssize_t objlen)
{
    if (start < 0) {
        start = 0;
    }
    if (start >= objlen) {
        start = objlen == 0 ? 0 : objlen - 1;
    }
    return start;
}

static Py_ssize_t
unicode_error_clip_end(Py_ssize_t end, Py_ssize_t objlen)
{
    if (end < 1) {
        end = 1;
    }
    if (end > objlen) {
        end = objlen;
    }
    return end;
}

PyObject *
PyUnicodeEncodeError_GetEncoding(PyObject *self)
{
    return check_unicode_error_type(self, "UnicodeEncodeError") < 0 ? NULL
        : unicode_error_attr(((PyUnicodeErrorObject *)self)->encoding, "encoding", 0);
}

PyObject *
PyUnicodeDecodeError_GetEncoding(PyObject *self)
{
    return check_unicode_error_type(self, "UnicodeDecodeError") < 0 ? NULL
        : unicode_error_attr(((PyUnicodeErrorObject *)self)->encoding, "encoding", 0);
}

PyObject *
PyUnicodeEncodeError_GetObject(PyObject *self)
{
    return check_unicode_error_type(self, "UnicodeEncodeError") < 0 ? NULL
        : unicode_error_attr(((PyUnicodeErrorObject *)self)->object, "object", 0);
}

PyObject *
PyUnicodeDecodeError_GetObject(PyObject *self)
{
    return check_unicode_error_type(self, "UnicodeDecodeError") < 0 ? NULL
        : unicode_error_attr(((PyUnicodeErrorObject *)self)->object, "object", 1);
}

PyObject *
PyUnicodeTranslateError_GetObject(PyObject *self)
{
    return check_unicode_error_type(self, "UnicodeTranslateError") < 0 ? NULL
        : unicode_error_attr(((PyUnicodeErrorObject *)self)->object, "object", 0);
}

#define UNICODE_ERROR_GET_RANGE(name, expect, as_bytes, field, clip) \
int \
name(PyObject *self, Py_ssize_t *value) \
{ \
    if (check_unicode_error_type(self, expect) < 0) { \
        return -1; \
    } \
    Py_ssize_t objlen; \
    if (unicode_error_object_len(self, &objlen, as_bytes) < 0) { \
        return -1; \
    } \
    *value = clip(((PyUnicodeErrorObject *)self)->field, objlen); \
    return 0; \
}

UNICODE_ERROR_GET_RANGE(PyUnicodeEncodeError_GetStart, "UnicodeEncodeError", 0, start, unicode_error_clip_start)
UNICODE_ERROR_GET_RANGE(PyUnicodeDecodeError_GetStart, "UnicodeDecodeError", 1, start, unicode_error_clip_start)
UNICODE_ERROR_GET_RANGE(PyUnicodeTranslateError_GetStart, "UnicodeTranslateError", 0, start, unicode_error_clip_start)
UNICODE_ERROR_GET_RANGE(PyUnicodeEncodeError_GetEnd, "UnicodeEncodeError", 0, end, unicode_error_clip_end)
UNICODE_ERROR_GET_RANGE(PyUnicodeDecodeError_GetEnd, "UnicodeDecodeError", 1, end, unicode_error_clip_end)
UNICODE_ERROR_GET_RANGE(PyUnicodeTranslateError_GetEnd, "UnicodeTranslateError", 0, end, unicode_error_clip_end)

#define UNICODE_ERROR_SET_RANGE(name, expect, which) \
int \
name(PyObject *self, Py_ssize_t value) \
{ \
    if (check_unicode_error_type(self, expect) < 0) { \
        return -1; \
    } \
    ((PyUnicodeErrorObject *)self)->which = value; \
    return 0; \
}

UNICODE_ERROR_SET_RANGE(PyUnicodeEncodeError_SetStart, "UnicodeEncodeError", start)
UNICODE_ERROR_SET_RANGE(PyUnicodeDecodeError_SetStart, "UnicodeDecodeError", start)
UNICODE_ERROR_SET_RANGE(PyUnicodeTranslateError_SetStart, "UnicodeTranslateError", start)
UNICODE_ERROR_SET_RANGE(PyUnicodeEncodeError_SetEnd, "UnicodeEncodeError", end)
UNICODE_ERROR_SET_RANGE(PyUnicodeDecodeError_SetEnd, "UnicodeDecodeError", end)
UNICODE_ERROR_SET_RANGE(PyUnicodeTranslateError_SetEnd, "UnicodeTranslateError", end)

PyObject *
PyUnicodeEncodeError_GetReason(PyObject *self)
{
    return check_unicode_error_type(self, "UnicodeEncodeError") < 0 ? NULL
        : unicode_error_attr(((PyUnicodeErrorObject *)self)->reason, "reason", 0);
}

PyObject *
PyUnicodeDecodeError_GetReason(PyObject *self)
{
    return check_unicode_error_type(self, "UnicodeDecodeError") < 0 ? NULL
        : unicode_error_attr(((PyUnicodeErrorObject *)self)->reason, "reason", 0);
}

PyObject *
PyUnicodeTranslateError_GetReason(PyObject *self)
{
    return check_unicode_error_type(self, "UnicodeTranslateError") < 0 ? NULL
        : unicode_error_attr(((PyUnicodeErrorObject *)self)->reason, "reason", 0);
}

#define UNICODE_ERROR_SET_REASON(name, expect) \
int \
name(PyObject *self, const char *reason) \
{ \
    if (check_unicode_error_type(self, expect) < 0) { \
        return -1; \
    } \
    PyObject *value = PyUnicode_FromString(reason); \
    if (value == NULL) { \
        return -1; \
    } \
    Py_XSETREF(((PyUnicodeErrorObject *)self)->reason, value); \
    return 0; \
}

UNICODE_ERROR_SET_REASON(PyUnicodeEncodeError_SetReason, "UnicodeEncodeError")
UNICODE_ERROR_SET_REASON(PyUnicodeDecodeError_SetReason, "UnicodeDecodeError")
UNICODE_ERROR_SET_REASON(PyUnicodeTranslateError_SetReason, "UnicodeTranslateError")

/* The exception group to raise after a try-except* ran (Objects/exceptions.c
   PyUnstable_Exc_PrepReraiseStar): the computation is the kernel's
   prep_reraise_star, which CALL_INTRINSIC_1 reaches for the opcode. */
PyObject *
PyUnstable_Exc_PrepReraiseStar(PyObject *orig, PyObject *excs)
{
    if (orig == NULL || !PyExceptionInstance_Check(orig)) {
        PyErr_SetString(PyExc_TypeError, "orig must be an exception instance");
        return NULL;
    }
    if (excs == NULL || !PyList_Check(excs)) {
        PyErr_SetString(PyExc_TypeError, "excs must be a list of exception instances");
        return NULL;
    }
    Py_ssize_t numexcs = PyList_GET_SIZE(excs);
    for (Py_ssize_t i = 0; i < numexcs; i++) {
        PyObject *exc = PyList_GET_ITEM(excs, i);
        if (!(PyExceptionInstance_Check(exc) || exc == Py_None)) {
            PyErr_Format(PyExc_TypeError, "item %zd of excs is not an exception", i);
            return NULL;
        }
    }
    PyObject *tb = PyException_GetTraceback(orig);
    if (tb == NULL) {
        PyErr_Format(PyExc_ValueError, "orig must be a raised exception");
        return NULL;
    }
    Py_DECREF(tb);
    PyObject *args[2] = { orig, excs };
    return lu_call_kernel(LU_K_prep_reraise_star, args, 2);
}

/* The handled exceptions (Python/errors.c): the topmost one that is not
   unset, walking the task's exc_info chain. */

_PyErr_StackItem *
lu_topmost_exc_info(PyThreadState *ts)
{
    _PyErr_StackItem *item = ts->exc_info;
    while (item->exc_value == NULL && item->previous_item != NULL) {
        item = item->previous_item;
    }
    return item;
}

PyObject *
PyErr_GetHandledException(void)
{
    PyObject *exc = lu_topmost_exc_info(PyThreadState_Get())->exc_value;
    return exc == NULL || exc == Py_None ? NULL : Py_NewRef(exc);
}

void
PyErr_SetHandledException(PyObject *exc)
{
    _PyErr_StackItem *item = PyThreadState_Get()->exc_info;
    PyObject *old = item->exc_value;
    item->exc_value = exc == Py_None ? NULL : Py_XNewRef(exc);
    Py_XDECREF(old);
}

void
PyErr_GetExcInfo(PyObject **type, PyObject **value, PyObject **tb)
{
    PyObject *exc = lu_topmost_exc_info(PyThreadState_Get())->exc_value;
    if (exc == NULL || exc == Py_None) {
        *type = *value = *tb = NULL;
        return;
    }
    *type = Py_NewRef((PyObject *)Py_TYPE(exc));
    *value = Py_NewRef(exc);
    *tb = Py_XNewRef(((PyBaseExceptionObject *)exc)->traceback);
}

void
PyErr_SetExcInfo(PyObject *type, PyObject *value, PyObject *tb)
{
    PyErr_SetHandledException(value);
    Py_XDECREF(type);
    Py_XDECREF(value);
    Py_XDECREF(tb);
}

/* get_exception_handler: the co_exceptiontable entry covering the code unit
   `index`, if any (InternalDocs/exception_handling.md). The table is a
   sorted run of varint entries: start, size, target, depth << 1 | lasti. */
static const unsigned char *
parse_varint(const unsigned char *p, int *result)
{
    int val = p[0] & 63;
    while (p[0] & 64) {
        p++;
        val = (val << 6) | (p[0] & 63);
    }
    *result = val;
    return p + 1;
}

int
lu_exception_handler(PyCodeObject *code, int index, int *level, int *handler, int *lasti)
{
    const unsigned char *scan = (const unsigned char *)PyBytes_AS_STRING(code->co_exceptiontable);
    const unsigned char *end = scan + PyBytes_GET_SIZE(code->co_exceptiontable);
    while (scan < end) {
        int start, size, depth_and_lasti;
        scan = parse_varint(scan, &start);
        if (start > index) {
            break;
        }
        scan = parse_varint(scan, &size);
        scan = parse_varint(scan, handler);
        scan = parse_varint(scan, &depth_and_lasti);
        if (start + size > index) {
            *level = depth_and_lasti >> 1;
            *lasti = depth_and_lasti & 1;
            return 1;
        }
    }
    return 0;
}

/* PyCode_Addr2Line over the location table (Objects/codeobject.c): the line
   of the instruction at byte offset `addrq`, or -1 when it has none. */
static int
scan_varint(const uint8_t **p)
{
    unsigned int read = *(*p)++;
    unsigned int val = read & 63, shift = 0;
    while (read & 64) {
        read = *(*p)++;
        shift += 6;
        val |= (read & 63) << shift;
    }
    return (int)val;
}

int
PyCode_Addr2Line(PyCodeObject *co, int addrq)
{
    if (addrq < 0) {
        return co->co_firstlineno;
    }
    const uint8_t *p = (const uint8_t *)PyBytes_AS_STRING(co->co_linetable);
    const uint8_t *limit = p + PyBytes_GET_SIZE(co->co_linetable);
    int line = co->co_firstlineno, start = 0;
    while (p < limit) {
        int code = (*p >> 3) & 15, delta = 0;
        const uint8_t *q = p + 1;
        if (code == PY_CODE_LOCATION_INFO_NO_COLUMNS || code == PY_CODE_LOCATION_INFO_LONG) {
            unsigned int u = (unsigned int)scan_varint(&q);
            delta = u & 1 ? -(int)(u >> 1) : (int)(u >> 1);
        }
        else if (code >= PY_CODE_LOCATION_INFO_ONE_LINE0 && code <= PY_CODE_LOCATION_INFO_ONE_LINE2) {
            delta = code - PY_CODE_LOCATION_INFO_ONE_LINE0;
        }
        line += delta;
        int end = start + ((*p & 7) + 1) * (int)sizeof(lu_codeunit);
        if (addrq < end) {
            return (*p >> 3) == 0x1f ? -1 : line;
        }
        start = end;
        do {
            p++;
        } while (p < limit && (*p & 128) == 0);
    }
    return -1;
}

/* Native raising with the context chain (Python/errors.c _PyErr_SetObject):
   `value` becomes the exception in flight with the handled exception as its
   __context__, unless that would close a cycle. */
void
lu_exc_chain_context(PyObject *value)
{
    PyObject *handled = lu_topmost_exc_info(PyThreadState_Get())->exc_value;
    if (handled == NULL || handled == Py_None || handled == value) {
        return;
    }
    PyObject *o = handled, *slow = handled, *context;
    int toggle = 0;
    while ((context = ((PyBaseExceptionObject *)o)->context) != NULL) {
        if (context == value) {
            PyException_SetContext(o, NULL);
            break;
        }
        o = context;
        if (o == slow) {
            break;
        }
        if (toggle) {
            slow = ((PyBaseExceptionObject *)slow)->context;
        }
        toggle = !toggle;
    }
    PyException_SetContext(value, Py_NewRef(handled));
}

/* Primitives for the exception classes. */

#define ARGS(n) \
    if (nargs != (n)) { \
        lu_err_format(PyExc_TypeError, "%s expected %d arguments, got %zd", __func__ + 5, (n), nargs); \
        return NULL; \
    }

/* exc_set_word(e, i, v): word i after the exception head, which no
   attribute can set (BaseExceptionGroup's message and exceptions). */
static PyObject *
prim_exc_set_word(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(3);
    const struct exc_layout *l = layout_of(Py_TYPE(args[0]));
    long i;
    if (!PyExceptionInstance_Check(args[0]) || l == NULL || lu_long_as_long(args[1], &i) < 0
        || i < 0 || (size_t)i >= l->nwords || (l->ssize >> i & 1)) {
        lu_err_format(PyExc_SystemError, "exc_set_word: bad exception or index");
        return NULL;
    }
    Py_XSETREF(words_of(args[0])[i], args[2] == Py_None ? NULL : Py_NewRef(args[2]));
    return Py_NewRef(Py_None);
}

/* exc_word(e, i): word i after the exception head, None when unset. */
static PyObject *
prim_exc_word(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2);
    const struct exc_layout *l = layout_of(Py_TYPE(args[0]));
    long i;
    if (!PyExceptionInstance_Check(args[0]) || l == NULL || lu_long_as_long(args[1], &i) < 0
        || i < 0 || (size_t)i >= l->nwords || (l->ssize >> i & 1)) {
        lu_err_format(PyExc_SystemError, "exc_word: bad exception or index");
        return NULL;
    }
    PyObject *v = words_of(args[0])[i];
    return Py_NewRef(v != NULL ? v : Py_None);
}

/* set_handled_exception(e): the exception handled by the running frame. */
static PyObject *
prim_set_handled_exception(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    PyErr_SetHandledException(args[0]);
    return Py_NewRef(Py_None);
}

/* is_callable(x): whether x's type has a tp_call (PyCallable_Check). */
static PyObject *
prim_is_callable(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    return Py_NewRef(Py_TYPE(args[0])->tp_call != NULL ? Py_True : Py_False);
}

/* int_text(n): the decimal text of a small int, for messages that name a
   count or a position. */
static PyObject *
prim_int_text(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    long n;
    if (!PyLong_Check(args[0]) || lu_long_as_long(args[0], &n) < 0) {
        lu_err_format(PyExc_TypeError, "int_text() needs a small int");
        return NULL;
    }
    char buf[32];
    snprintf(buf, sizeof buf, "%ld", n);
    return PyUnicode_FromString(buf);
}

/* caller_traceback(): a traceback of one entry for the Python frame that
   called the running kernel function, as of its current instruction. */
static PyObject *
prim_caller_traceback(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(0);
    struct lu_frame *f = ((struct lu_frame *)PyThreadState_Get()->current_frame)->previous;
    PyObject *fr = lu_frame_object(f);
    PyObject *tb = fr != NULL
        ? (PyObject *)tb_new(fr, NULL, (int)(lu_frame_lasti(f) * sizeof(lu_codeunit))) : NULL;
    Py_XDECREF(fr);
    return tb;
}

/* handled_exception(): the exception being handled, or None. */
static PyObject *
prim_handled_exception(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(0);
    PyObject *exc = PyErr_GetHandledException();
    return exc != NULL ? exc : Py_NewRef(Py_None);
}

/* is_exception(x): whether x is an exception instance. */
static PyObject *
prim_is_exception(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    return Py_NewRef(PyExceptionInstance_Check(args[0]) ? Py_True : Py_False);
}

/* is_exception_class(x): whether x is a class deriving from BaseException. */
static PyObject *
prim_is_exception_class(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    return Py_NewRef(PyExceptionClass_Check(args[0]) ? Py_True : Py_False);
}

/* write_unraisable(message, obj, exc): prints "message repr(obj):" and exc
   with its traceback to stderr, as PyErr_FormatUnraisable does. */
static PyObject *
prim_write_unraisable(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    if (nargs != 3 || !PyUnicode_Check(args[0]) || !PyExceptionInstance_Check(args[2])) {
        lu_err_format(PyExc_TypeError, "write_unraisable(message, obj, exc)");
        return NULL;
    }
    PyErr_SetRaisedException(Py_NewRef(args[2]));
    lu_err_unraisable_text(PyUnicode_AsUTF8(args[0]), args[1]);
    return Py_NewRef(Py_None);
}

/* err_display(exc): prints exc and its chain with tracebacks to stderr, as the
   default excepthook does; any other object is printed as its str. */
static PyObject *
prim_err_display(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    if (PyExceptionInstance_Check(args[0])) {
        lu_err_display(args[0]);
    }
    else {
        PyObject *text = PyObject_Str(args[0]);
        if (text == NULL) {
            return NULL;
        }
        lu_stderr_write(PyUnicode_AsUTF8(text), strlen(PyUnicode_AsUTF8(text)));
        lu_stderr_write("\n", 1);
        Py_DECREF(text);
    }
    return Py_NewRef(Py_None);
}

#define FAST(name) { #name, (PyCFunction)(void (*)(void))prim_##name, METH_FASTCALL, NULL }
PyMethodDef lu_prim_exc_methods[] = {
    FAST(write_unraisable), FAST(err_display),
    FAST(exc_set_word), FAST(exc_word), FAST(handled_exception), FAST(set_handled_exception),
    FAST(is_exception), FAST(is_exception_class), FAST(caller_traceback),
    FAST(is_callable), FAST(int_text),
    { NULL, NULL, 0, NULL },
};

/* Startup: sizes, attributes and deallocs of the exception, traceback and
   frame types, before the types are made ready. */
void
lu_exc_init(void)
{
    for (PyTypeObject *const *tp = lu_exception_types; *tp != NULL; tp++) {
        const struct exc_layout *l = layout_of(*tp);
        (*tp)->tp_basicsize = sizeof(PyBaseExceptionObject)
            + (l != NULL ? l->nwords * sizeof(PyObject *) : 0);
        (*tp)->tp_flags |= Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC;
        (*tp)->tp_dealloc = exc_dealloc;
        (*tp)->tp_traverse = exc_traverse;
        (*tp)->tp_clear = exc_clear;
        (*tp)->tp_dictoffset = offsetof(PyBaseExceptionObject, dict);
    }
    static const struct { PyObject **type; PyMemberDef *members; PyGetSetDef *getset; } own[] = {
        { &PyExc_BaseException, base_members, base_getset },
        { &PyExc_SystemExit, system_exit_members, NULL },
        { &PyExc_StopIteration, stop_iteration_members, NULL },
        { &PyExc_NameError, name_error_members, NULL },
        { &PyExc_AttributeError, attribute_error_members, NULL },
        { &PyExc_ImportError, import_error_members, NULL },
        { &PyExc_SyntaxError, syntax_error_members, NULL },
        { &PyExc_UnicodeError, unicode_error_members, NULL },
        { &PyExc_OSError, os_error_members, os_error_getset },
        { &PyExc_BaseExceptionGroup, group_members, NULL },
    };
    for (size_t i = 0; i < sizeof own / sizeof own[0]; i++) {
        PyTypeObject *tp = (PyTypeObject *)*own[i].type;
        tp->tp_members = own[i].members;
        tp->tp_getset = own[i].getset;
    }
    ((PyTypeObject *)PyExc_OSError)->tp_alloc = oserror_alloc;
    PyTraceBack_Type.tp_basicsize = sizeof(PyTracebackObject);
    PyTraceBack_Type.tp_flags |= Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC;
    PyTraceBack_Type.tp_dealloc = tb_dealloc;
    PyTraceBack_Type.tp_traverse = tb_traverse;
    PyTraceBack_Type.tp_clear = tb_clear;
    PyTraceBack_Type.tp_members = tb_members;
    PyTraceBack_Type.tp_getset = tb_getset;
}
