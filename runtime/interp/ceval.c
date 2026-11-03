/* The eval loop (DESIGN 6.1): computed-goto dispatch over CPython 3.14
   bytecode, semantics from Python/bytecodes.c written by hand. One
   activation runs all of a task's Python frames: a Python call pushes a frame
   and dispatches into it, a return pops it, and only C callers start a new
   activation (lu_eval). The loop polls the carrier's word at calls and
   back-edges, and RESUME compares the code's monitoring version.

   Stack entries are strong references. An error joins the exception's
   traceback, then looks the raising instruction up in the frame's exception
   table: a handler gets the stack cut to its depth, the raising offset when
   it asks for it, and the exception; no handler pops the frame and the
   caller's loop takes up the error (zero-cost exceptions, 3.11+).

   Attribute access, calls of kernel functions and class bodies stay in the
   loop too: where a type's slot is the kernel's (a thunk), the loop pushes
   the kernel Python function's frame instead of calling the slot, and the
   frame's `ret` says what the caller does with its value (DESIGN 5.2). */

#include "interp/frame.h"
#include "interp/code.h"
#include "interp/exceptions.h"

#include "opcode_ids.h"

/* Include/internal/pycore_opcode_utils.h */
#define MAKE_FUNCTION_DEFAULTS    0x01
#define MAKE_FUNCTION_KWDEFAULTS  0x02
#define MAKE_FUNCTION_ANNOTATIONS 0x04
#define MAKE_FUNCTION_CLOSURE     0x08
#define MAKE_FUNCTION_ANNOTATE    0x10
#define RESUME_AFTER_YIELD_FROM 2
#define RESUME_OPARG_LOCATION_MASK 0x3

/* _PyOpcode_Caches: CACHE entries after each instruction. */
static const uint8_t caches[256] = {
    [TO_BOOL] = 3, [STORE_SUBSCR] = 1, [SEND] = 1, [UNPACK_SEQUENCE] = 1,
    [STORE_ATTR] = 4, [LOAD_GLOBAL] = 4, [LOAD_SUPER_ATTR] = 1, [LOAD_ATTR] = 9,
    [COMPARE_OP] = 1, [CONTAINS_OP] = 1, [JUMP_BACKWARD] = 1, [POP_JUMP_IF_TRUE] = 1,
    [POP_JUMP_IF_FALSE] = 1, [POP_JUMP_IF_NONE] = 1, [POP_JUMP_IF_NOT_NONE] = 1,
    [FOR_ITER] = 1, [CALL] = 3, [CALL_KW] = 3, [CALL_FUNCTION_EX] = 0, [BINARY_OP] = 5,
};

#define NAME_ERROR_MSG "name '%.200s' is not defined"
#define UNBOUNDLOCAL_ERROR_MSG \
    "cannot access local variable '%s' where it is not associated with a value"

#define INTRINSIC_PREP_RERAISE_STAR 1   /* pycore_intrinsics.h */
#define INTRINSIC_LIST_TO_TUPLE 6

#define CANNOT_CATCH_MSG \
    "catching classes that do not inherit from BaseException is not allowed"

/* _PyEval_CheckExceptTypeValid. */
static int
check_except_type(PyObject *right)
{
    if (PyTuple_Check(right)) {
        for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(right); i++) {
            if (!PyExceptionClass_Check(PyTuple_GET_ITEM(right, i))) {
                lu_err_format(PyExc_TypeError, CANNOT_CATCH_MSG);
                return -1;
            }
        }
    }
    else if (!PyExceptionClass_Check(right)) {
        lu_err_format(PyExc_TypeError, CANNOT_CATCH_MSG);
        return -1;
    }
    return 0;
}

#define UNBOUNDFREE_ERROR_MSG \
    "cannot access free variable '%s' where it is not associated with a value" \
    " in enclosing scope"

/* _PyEval_FormatExcUnbound: the local at `index` has no value. */
static void
format_unbound(PyCodeObject *co, int index)
{
    const char *name = PyUnicode_AsUTF8(PyTuple_GET_ITEM(co->co_localsplusnames, index));
    if (index < PyUnstable_Code_GetFirstFree(co)) {
        lu_err_format(PyExc_UnboundLocalError, UNBOUNDLOCAL_ERROR_MSG, name);
    }
    else {
        lu_err_format(PyExc_NameError, UNBOUNDFREE_ERROR_MSG, name);
    }
}

/* PyObject_IsTrue over the types that exist so far: the singletons, str,
   exact tuples, lists and dicts, and objects that define no truth. */
static int
to_bool(PyObject *v)
{
    if (v == Py_True || v == Py_False) {
        return v == Py_True;
    }
    if (v == Py_None) {
        return 0;
    }
    if (PyUnicode_CheckExact(v)) {
        return PyUnicode_GET_LENGTH(v) != 0;
    }
    if (PyTuple_CheckExact(v) || PyList_CheckExact(v)) {
        return Py_SIZE(v) != 0;
    }
    if (PyDict_CheckExact(v)) {
        return ((PyDictObject *)v)->ma_used != 0;
    }
    PyTypeObject *tp = Py_TYPE(v);
    if ((tp->tp_as_number == NULL || tp->tp_as_number->nb_bool == NULL)
        && (tp->tp_as_mapping == NULL || tp->tp_as_mapping->mp_length == NULL)
        && (tp->tp_as_sequence == NULL || tp->tp_as_sequence->sq_length == NULL)) {
        return 1;
    }
    lu_err_format(PyExc_SystemError, "truth of a %s arrives with its type", tp->tp_name);
    return -1;
}

static void
name_error(PyObject *name)
{
    lu_err_format(PyExc_NameError, NAME_ERROR_MSG, PyUnicode_AsUTF8(name));
}

/* LOAD_NAME and LOAD_GLOBAL over namespace dicts; mappings that are not
   exact dicts arrive with the kernel's mapping protocol (K1c). */
static PyObject *
ns_get(PyObject *ns, PyObject *name)
{
    if (!PyDict_CheckExact(ns)) {
        lu_err_format(PyExc_SystemError, "namespace is a %s, not a dict", Py_TYPE(ns)->tp_name);
        return NULL;
    }
    return lu_ns_lookup(ns, name);
}

static PyObject *
import_name(PyThreadState *ts, PyObject *name, PyObject *fromlist, PyObject *level)
{
    /* PyImport_ImportModuleLevelObject's sys.modules hit for absolute imports;
       importlib takes over the rest at K1n. */
    if (level != lu_small_int(0)) {
        lu_err_format(PyExc_ImportError, "relative imports arrive with importlib");
        return NULL;
    }
    PyObject *mod = lu_ns_lookup(ts->interp->modules, name);
    if (mod == NULL) {
        lu_err_format(PyExc_ModuleNotFoundError, "No module named '%s'", PyUnicode_AsUTF8(name));
        return NULL;
    }
    return Py_NewRef(mod);
}

static PyObject *
import_from(PyObject *module, PyObject *name)
{
    PyObject *v = PyModule_CheckExact(module)
        ? lu_ns_lookup(((PyModuleObject *)module)->md_dict, name) : NULL;
    if (v == NULL) {
        lu_err_format(PyExc_ImportError, "cannot import name '%s' from '%s' (unknown location)",
                      PyUnicode_AsUTF8(name),
                      PyModule_CheckExact(module)
                          ? PyUnicode_AsUTF8(((PyModuleObject *)module)->md_name) : "?");
        return NULL;
    }
    return Py_NewRef(v);
}

/* GET_ITER and FOR_ITER over exact tuples and lists (_PyTupleIterObject,
   _PyListIterObject); other iterables through their C slots until the
   iteration protocol is kernel Python (K1h). */
typedef struct {
    PyObject_HEAD
    Py_ssize_t it_index;
    PyObject *it_seq;
} seqiterobject;

static PyObject *
get_iter(PyObject *v)
{
    PyTypeObject *tp = Py_TYPE(v);
    if (tp == &PyTuple_Type || tp == &PyList_Type) {
        PyTypeObject *it_type = tp == &PyTuple_Type ? &PyTupleIter_Type : &PyListIter_Type;
        seqiterobject *it = (seqiterobject *)lu_object_new(it_type, sizeof(seqiterobject));
        if (it != NULL) {
            it->it_seq = Py_NewRef(v);
        }
        return (PyObject *)it;
    }
    if (tp->tp_iter != NULL) {
        return tp->tp_iter(v);
    }
    lu_err_format(PyExc_TypeError, "'%.200s' object is not iterable", tp->tp_name);
    return NULL;
}

/* The next item, or NULL: exhausted when no error is set. */
static PyObject *
iter_next(PyObject *iter)
{
    PyTypeObject *tp = Py_TYPE(iter);
    if (tp == &PyTupleIter_Type || tp == &PyListIter_Type) {
        seqiterobject *it = (seqiterobject *)iter;
        if (it->it_seq == NULL) {
            return NULL;
        }
        if (it->it_index < Py_SIZE(it->it_seq)) {
            PyObject *v = tp == &PyTupleIter_Type ? PyTuple_GET_ITEM(it->it_seq, it->it_index)
                                                  : PyList_GET_ITEM(it->it_seq, it->it_index);
            it->it_index++;
            return Py_NewRef(v);
        }
        Py_CLEAR(it->it_seq);
        return NULL;
    }
    if (tp->tp_iternext == NULL) {
        lu_err_format(PyExc_TypeError, "'%.100s' object is not an iterator", tp->tp_name);
        return NULL;
    }
    PyObject *v = tp->tp_iternext(iter);
    if (v == NULL && PyErr_ExceptionMatches(PyExc_StopIteration)) {
        PyErr_Clear();
    }
    return v;
}

static void
seqiter_dealloc(PyObject *op)
{
    Py_XDECREF(((seqiterobject *)op)->it_seq);
    lu_object_free(op);
}

void
lu_iter_init(void)
{
    PyTypeObject *types[] = { &PyTupleIter_Type, &PyListIter_Type };
    for (int i = 0; i < 2; i++) {
        types[i]->tp_basicsize = sizeof(seqiterobject);
        types[i]->tp_flags |= Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC;
        types[i]->tp_dealloc = seqiter_dealloc;
    }
}

/* UNPACK_SEQUENCE over exact tuples and lists: the items, last first. */
static int
unpack(PyObject *seq, int n, PyObject **out)
{
    if (!PyTuple_CheckExact(seq) && !PyList_CheckExact(seq)) {
        lu_err_format(PyExc_SystemError, "unpacking a %s arrives with iteration (K1h)",
                      Py_TYPE(seq)->tp_name);
        return -1;
    }
    Py_ssize_t size = Py_SIZE(seq);
    if (size != n) {
        if (size < n) {
            lu_err_format(PyExc_ValueError, "not enough values to unpack (expected %d, got %zd)",
                          n, size);
        }
        else {
            lu_err_format(PyExc_ValueError, "too many values to unpack (expected %d, got %zd)",
                          n, size);
        }
        return -1;
    }
    PyObject **items = PyTuple_CheckExact(seq) ? &PyTuple_GET_ITEM(seq, 0) : ((PyListObject *)seq)->ob_item;
    for (int i = 0; i < n; i++) {
        out[n - 1 - i] = Py_NewRef(items[i]);
    }
    return 0;
}

static void
set_function_attribute(PyFunctionObject *func, int which, PyObject *attr)
{
    PyObject **field;
    switch (which) {
    case MAKE_FUNCTION_DEFAULTS: field = &func->func_defaults; break;
    case MAKE_FUNCTION_KWDEFAULTS: field = &func->func_kwdefaults; break;
    case MAKE_FUNCTION_ANNOTATIONS: field = &func->func_annotations; break;
    case MAKE_FUNCTION_CLOSURE: field = &func->func_closure; break;
    default: field = &func->func_annotate; break;
    }
    Py_XSETREF(*field, attr);
}

PyObject *
lu_eval(PyThreadState *ts, struct lu_frame *frame)
{
    /* Ranges first, then the opcodes that override them. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverride-init"
    static void *const targets[256] = {
        [0 ... 255] = &&unknown_opcode,
        [MIN_INSTRUMENTED_OPCODE ... 255] = &&instrumented_opcode,
        [CACHE] = &&op_CACHE,
        [NOP] = &&op_NOP,
        [EXTENDED_ARG] = &&op_EXTENDED_ARG,
        [RESUME] = &&op_RESUME,
        [LOAD_CONST] = &&op_LOAD_CONST,
        [LOAD_SMALL_INT] = &&op_LOAD_SMALL_INT,
        [LOAD_NAME] = &&op_LOAD_NAME,
        [STORE_NAME] = &&op_STORE_NAME,
        [LOAD_GLOBAL] = &&op_LOAD_GLOBAL,
        [LOAD_FAST] = &&op_LOAD_FAST,
        [LOAD_FAST_BORROW] = &&op_LOAD_FAST,
        [LOAD_FAST_CHECK] = &&op_LOAD_FAST_CHECK,
        [LOAD_FAST_LOAD_FAST] = &&op_LOAD_FAST_LOAD_FAST,
        [LOAD_FAST_BORROW_LOAD_FAST_BORROW] = &&op_LOAD_FAST_LOAD_FAST,
        [STORE_FAST] = &&op_STORE_FAST,
        [STORE_FAST_LOAD_FAST] = &&op_STORE_FAST_LOAD_FAST,
        [STORE_FAST_STORE_FAST] = &&op_STORE_FAST_STORE_FAST,
        [PUSH_NULL] = &&op_PUSH_NULL,
        [POP_TOP] = &&op_POP_TOP,
        [CALL] = &&op_CALL,
        [CALL_KW] = &&op_CALL_KW,
        [CALL_FUNCTION_EX] = &&op_CALL_FUNCTION_EX,
        [MAKE_CELL] = &&op_MAKE_CELL,
        [COPY_FREE_VARS] = &&op_COPY_FREE_VARS,
        [LOAD_DEREF] = &&op_LOAD_DEREF,
        [STORE_DEREF] = &&op_STORE_DEREF,
        [DELETE_DEREF] = &&op_DELETE_DEREF,
        [LOAD_FROM_DICT_OR_DEREF] = &&op_LOAD_FROM_DICT_OR_DEREF,
        [DELETE_FAST] = &&op_DELETE_FAST,
        [LOAD_FAST_AND_CLEAR] = &&op_LOAD_FAST_AND_CLEAR,
        [STORE_GLOBAL] = &&op_STORE_GLOBAL,
        [DELETE_GLOBAL] = &&op_DELETE_GLOBAL,
        [DELETE_NAME] = &&op_DELETE_NAME,
        [COPY] = &&op_COPY,
        [SWAP] = &&op_SWAP,
        [NOT_TAKEN] = &&op_NOP,
        [TO_BOOL] = &&op_TO_BOOL,
        [UNARY_NOT] = &&op_UNARY_NOT,
        [IS_OP] = &&op_IS_OP,
        [POP_JUMP_IF_FALSE] = &&op_POP_JUMP_IF_FALSE,
        [POP_JUMP_IF_TRUE] = &&op_POP_JUMP_IF_TRUE,
        [POP_JUMP_IF_NONE] = &&op_POP_JUMP_IF_NONE,
        [POP_JUMP_IF_NOT_NONE] = &&op_POP_JUMP_IF_NOT_NONE,
        [JUMP_FORWARD] = &&op_JUMP_FORWARD,
        [JUMP_BACKWARD_NO_INTERRUPT] = &&op_JUMP_BACKWARD_NO_INTERRUPT,
        [BUILD_TUPLE] = &&op_BUILD_TUPLE,
        [BUILD_LIST] = &&op_BUILD_LIST,
        [LIST_APPEND] = &&op_LIST_APPEND,
        [LIST_EXTEND] = &&op_LIST_EXTEND,
        [DICT_MERGE] = &&op_DICT_MERGE,
        [DICT_UPDATE] = &&op_DICT_UPDATE,
        [CALL_INTRINSIC_1] = &&op_CALL_INTRINSIC_1,
        [RETURN_VALUE] = &&op_RETURN_VALUE,
        [MAKE_FUNCTION] = &&op_MAKE_FUNCTION,
        [SET_FUNCTION_ATTRIBUTE] = &&op_SET_FUNCTION_ATTRIBUTE,
        [BUILD_MAP] = &&op_BUILD_MAP,
        [IMPORT_NAME] = &&op_IMPORT_NAME,
        [IMPORT_FROM] = &&op_IMPORT_FROM,
        [JUMP_BACKWARD] = &&op_JUMP_BACKWARD,
        [LOAD_ATTR] = &&op_LOAD_ATTR,
        [STORE_ATTR] = &&op_STORE_ATTR,
        [DELETE_ATTR] = &&op_DELETE_ATTR,
        [LOAD_SUPER_ATTR] = &&op_LOAD_SUPER_ATTR,
        [LOAD_BUILD_CLASS] = &&op_LOAD_BUILD_CLASS,
        [LOAD_LOCALS] = &&op_LOAD_LOCALS,
        [LOAD_FROM_DICT_OR_GLOBALS] = &&op_LOAD_FROM_DICT_OR_GLOBALS,
        [SETUP_ANNOTATIONS] = &&op_SETUP_ANNOTATIONS,
        [GET_ITER] = &&op_GET_ITER,
        [FOR_ITER] = &&op_FOR_ITER,
        [END_FOR] = &&op_POP_TOP,
        [POP_ITER] = &&op_POP_TOP,
        [UNPACK_SEQUENCE] = &&op_UNPACK_SEQUENCE,
        [COMPARE_OP] = &&op_COMPARE_OP,
        [RAISE_VARARGS] = &&op_RAISE_VARARGS,
        [RERAISE] = &&op_RERAISE,
        [PUSH_EXC_INFO] = &&op_PUSH_EXC_INFO,
        [POP_EXCEPT] = &&op_POP_EXCEPT,
        [CHECK_EXC_MATCH] = &&op_CHECK_EXC_MATCH,
        [CHECK_EG_MATCH] = &&op_CHECK_EG_MATCH,
        [WITH_EXCEPT_START] = &&op_WITH_EXCEPT_START,
        [LOAD_SPECIAL] = &&op_LOAD_SPECIAL,
        [LOAD_COMMON_CONSTANT] = &&op_LOAD_COMMON_CONSTANT,
        [CALL_INTRINSIC_2] = &&op_CALL_INTRINSIC_2,
    };
#pragma GCC diagnostic pop
    /* An activation never changes carrier: an unpinned park returns from it. */
    const uintptr_t *poll = &lu_carrier->poll;
    struct lu_frame entry = { .owner = LU_FRAME_C_ENTRY };
    entry.previous = (struct lu_frame *)ts->current_frame;
    frame->previous = &entry;
    ts->current_frame = (struct _PyInterpreterFrame *)frame;

    PyCodeObject *code;
    PyObject **localsplus, **sp;
    lu_codeunit *next_instr;
    int opcode, oparg;
    Py_ssize_t lasti;           /* the raising instruction, in code units, for the unwinder */
    PyObject *result;
    /* The call tail's operands: the callable and the owned argument values,
       positional first, then the keywords named by kwnames. */
    PyObject *callable, *kwnames, **args, **base, **to_free;
    Py_ssize_t total, npos;
    int free_self;              /* args[-1] may take a prepended self */
    struct lu_frame *callee;
    /* A kernel call's operands: kernel[kid] on kargs[0..nk), owned, its
       value handled per kret. kret is also the value's fate for the callee
       push_callee enters. */
    PyObject *kargs[3];
    PyObject *exit_args[5];     /* WITH_EXCEPT_START's call: a free slot, self, 3 arguments */
    int kid, nk;
    uint8_t kret;

#define LOAD_FRAME() do { \
        code = frame->code; \
        localsplus = frame->localsplus; \
        sp = frame->stackpointer; \
        next_instr = frame->instr; \
    } while (0)
#define SAVE_FRAME() do { frame->instr = next_instr; frame->stackpointer = sp; } while (0)
#define DISPATCH() do { \
        opcode = next_instr->op.code; \
        oparg = next_instr->op.arg; \
        next_instr++; \
        goto *targets[opcode]; \
    } while (0)
#define SKIP_CACHES() (next_instr += caches[opcode])
/* The start of the instruction `next_instr` follows, caches included. */
#define INSTR_START() ({ \
        lu_codeunit *at = next_instr; \
        while ((at - 1)->op.code == CACHE) { \
            at--; \
        } \
        at - 1; })
#define PUSH(v) (*sp++ = (v))
#define POP() (*--sp)
#define TOP() (sp[-1])
#define GETLOCAL(i) (localsplus[i])
#define NAME(i) PyTuple_GET_ITEM(code->co_names, i)
#define ERROR_IF(cond) do { if (cond) goto error; } while (0)
/* A returned value, per the callee's enum lu_frame_return. */
#define PUSH_RETURNED(ret, v) do { \
        if ((ret) == LU_RETURN_DISCARD) { \
            Py_DECREF(v); \
        } \
        else if ((ret) == LU_RETURN_PAIR) { \
            PUSH(Py_NewRef(PyTuple_GET_ITEM(v, 0))); \
            PUSH(Py_NewRef(PyTuple_GET_ITEM(v, 1))); \
            Py_DECREF(v); \
        } \
        else { \
            PUSH(v); \
            if ((ret) == LU_RETURN_METHOD) { \
                PUSH(NULL); \
            } \
        } \
    } while (0)
#define POLL() do { \
        if (*poll & LU_POLL_EVENTS_MASK) { \
            SAVE_FRAME(); \
            ERROR_IF(lu_poll_slow(ts) < 0); \
        } \
    } while (0)

    LOAD_FRAME();
    DISPATCH();

op_CACHE:
unknown_opcode:
    SAVE_FRAME();
    lu_err_format(PyExc_SystemError, "%s:%d: unknown opcode %d",
                  PyUnicode_AsUTF8(code->co_filename), code->co_firstlineno, opcode);
    goto error;

instrumented_opcode:
    SAVE_FRAME();
    lu_err_format(PyExc_SystemError, "instrumented opcode %d without monitoring", opcode);
    goto error;

op_EXTENDED_ARG:
    opcode = next_instr->op.code;
    oparg = oparg << 8 | next_instr->op.arg;
    next_instr++;
    goto *targets[opcode];

op_NOP:
    DISPATCH();

op_RESUME: {
    uintptr_t version = *poll & ~LU_POLL_EVENTS_MASK;
    if (code->_co_instrumentation_version != version) {
        SAVE_FRAME();
        lu_err_format(PyExc_SystemError, "sys.monitoring arrives with M0.3");
        goto error;
    }
    if ((oparg & RESUME_OPARG_LOCATION_MASK) < RESUME_AFTER_YIELD_FROM) {
        POLL();
    }
    DISPATCH();
}

op_LOAD_CONST:
    PUSH(Py_NewRef(PyTuple_GET_ITEM(code->co_consts, oparg)));
    DISPATCH();

op_LOAD_SMALL_INT:
    PUSH(lu_small_int(oparg));
    DISPATCH();

op_LOAD_NAME: {
    PyObject *name = NAME(oparg), *v;
    if (frame->locals == NULL) {
        SAVE_FRAME();
        lu_err_format(PyExc_SystemError, "no locals found");
        goto error;
    }
    if ((v = ns_get(frame->locals, name)) == NULL && !PyErr_Occurred()
        && (v = ns_get(frame->globals, name)) == NULL && !PyErr_Occurred()
        && (v = ns_get(frame->builtins, name)) == NULL && !PyErr_Occurred()) {
        name_error(name);
    }
    ERROR_IF(v == NULL);
    PUSH(Py_NewRef(v));
    DISPATCH();
}

op_STORE_NAME: {
    PyObject *name = NAME(oparg);
    PyObject *v = POP();
    SAVE_FRAME();
    int err;
    if (frame->locals == NULL) {
        lu_err_format(PyExc_SystemError, "no locals found when storing '%s'",
                      PyUnicode_AsUTF8(name));
        err = -1;
    }
    else {
        err = ns_get(frame->locals, name) == NULL && PyErr_Occurred()
            ? -1 : lu_ns_insert(frame->locals, name, v);
    }
    Py_DECREF(v);
    ERROR_IF(err < 0);
    DISPATCH();
}

op_LOAD_GLOBAL: {
    PyObject *name = NAME(oparg >> 1), *v;
    SKIP_CACHES();
    if ((v = ns_get(frame->globals, name)) == NULL && !PyErr_Occurred()
        && (v = ns_get(frame->builtins, name)) == NULL && !PyErr_Occurred()) {
        name_error(name);
    }
    ERROR_IF(v == NULL);
    PUSH(Py_NewRef(v));
    if (oparg & 1) {
        PUSH(NULL);
    }
    DISPATCH();
}

op_LOAD_FAST:
    PUSH(Py_NewRef(GETLOCAL(oparg)));
    DISPATCH();

op_LOAD_FAST_CHECK: {
    PyObject *v = GETLOCAL(oparg);
    if (v == NULL) {
        SAVE_FRAME();
        format_unbound(code, oparg);
        goto error;
    }
    PUSH(Py_NewRef(v));
    DISPATCH();
}

op_LOAD_FAST_LOAD_FAST:
    PUSH(Py_NewRef(GETLOCAL(oparg >> 4)));
    PUSH(Py_NewRef(GETLOCAL(oparg & 15)));
    DISPATCH();

op_STORE_FAST:
    Py_XSETREF(GETLOCAL(oparg), POP());
    DISPATCH();

op_STORE_FAST_LOAD_FAST:
    Py_XSETREF(GETLOCAL(oparg >> 4), POP());
    PUSH(Py_NewRef(GETLOCAL(oparg & 15)));
    DISPATCH();

op_STORE_FAST_STORE_FAST:
    Py_XSETREF(GETLOCAL(oparg >> 4), POP());
    Py_XSETREF(GETLOCAL(oparg & 15), POP());
    DISPATCH();

op_PUSH_NULL:
    PUSH(NULL);
    DISPATCH();

op_POP_TOP:
    Py_XDECREF(POP());
    DISPATCH();

op_CALL_KW:
    SKIP_CACHES();
    kwnames = POP();
    goto call_stack_args;

op_CALL:
    SKIP_CACHES();
    kwnames = NULL;
call_stack_args:
    /* callable, self_or_null, args[oparg] */
    base = sp - oparg - 2;
    args = sp - oparg;
    callable = args[-2];
    total = oparg;
    if (args[-1] != NULL) {
        args--;
        total++;
    }
    npos = total - (kwnames != NULL ? PyTuple_GET_SIZE(kwnames) : 0);
    free_self = args[-1] == NULL;
    to_free = NULL;

call_tail: {
    /* Consumes callable, args and kwnames; the stack is reset to base. A
       bound method, a kernel function and an object whose __call__ is
       Python run in this loop; everything else is foreign C. */
    SAVE_FRAME();
    if (Py_IS_TYPE(callable, &PyMethod_Type) && free_self) {
        PyMethodObject *m = (PyMethodObject *)callable;
        *--args = Py_NewRef(m->im_self);
        npos++;
        total++;
        free_self = 0;
        PyObject *func = Py_NewRef(m->im_func);
        Py_SETREF(callable, func);
    }
    if (callable == (PyObject *)&PyType_Type && total == 1 && npos == 1) {
        result = Py_NewRef((PyObject *)Py_TYPE(args[0]));       /* type(x) */
        goto call_done;
    }
    PyObject *target = NULL, *self = NULL;
    if (Py_IS_TYPE(callable, &PyFunction_Type)) {
        target = callable;
    }
    else if (Py_IS_TYPE(callable, &PyCFunction_Type)
             && (((PyCFunctionObject *)callable)->m_ml->ml_flags & LU_METH_FRAME)) {
        if (kwnames != NULL) {
            lu_err_format(PyExc_TypeError, "%s() takes no keyword arguments",
                          ((PyCFunctionObject *)callable)->m_ml->ml_name);
            result = NULL;
            goto call_done;
        }
        struct lu_frame *(*prim)(PyThreadState *, PyObject *const *, Py_ssize_t) =
            (struct lu_frame *(*)(PyThreadState *, PyObject *const *, Py_ssize_t))(void (*)(void))
            ((PyCFunctionObject *)callable)->m_ml->ml_meth;
        callee = prim(ts, args, npos);
        for (Py_ssize_t i = 0; i < total; i++) {
            Py_DECREF(args[i]);
        }
        Py_DECREF(callable);
        PyMem_Free(to_free);
        sp = base;
        frame->stackpointer = sp;
        ERROR_IF(callee == NULL);
        kret = LU_RETURN_PUSH;
        goto push_callee;
    }
    else {
        int found = lu_kernel_target(callable, args, npos, &target, &self);
        if (found < 0) {
            result = NULL;
            goto call_done;
        }
        if (!found && Py_TYPE(callable)->tp_call == lu_slot_tp_call) {
            /* An instance whose type's __call__ is a function or kernel descriptor. */
            PyObject *descr = _PyType_Lookup(Py_TYPE(callable), LU_ID(__call__));
            PyObject *unused;
            if (descr != NULL && (PyFunction_Check(descr)
                                  || (lu_kernel_target(descr, &callable, 1, &target, &unused) > 0
                                      && unused == NULL))) {
                target = PyFunction_Check(descr) ? descr : target;
                self = callable;
            }
            else {
                target = NULL;
            }
        }
    }
    if (target != NULL && (self == NULL || free_self)) {
        if (self != NULL) {
            *--args = Py_NewRef(self);
            npos++;
        }
        callee = lu_frame_call(ts, (PyFunctionObject *)target, args, npos, kwnames);
        Py_XDECREF(kwnames);
        Py_DECREF(callable);
        PyMem_Free(to_free);
        sp = base;
        frame->stackpointer = sp;
        ERROR_IF(callee == NULL);
        kret = LU_RETURN_PUSH;
        goto push_callee;
    }
    /* Foreign C pins the task; kernel primitives will be exempted when
       migration is on (DESIGN 5.4). */
    struct lu_task *t = (struct lu_task *)ts;
    t->pin++;
    result = lu_call(callable, args, npos, kwnames);
    t->pin--;
call_done:
    for (Py_ssize_t i = 0; i < total; i++) {
        Py_DECREF(args[i]);
    }
    Py_XDECREF(kwnames);
    Py_DECREF(callable);
    PyMem_Free(to_free);
    sp = base;
    ERROR_IF(result == NULL);
    PUSH(result);
    POLL();
    DISPATCH();
}

kernel_call: {
    /* kernel[kid](*kargs[:nk]) in this loop. */
    SAVE_FRAME();
    PyObject *kf = ts->interp->kernel[kid];
    if (kf == NULL) {
        for (int i = 0; i < nk; i++) {
            Py_DECREF(kargs[i]);
        }
        lu_err_format(PyExc_SystemError, "kernel function %d is not registered yet", kid);
        goto error;
    }
    callee = lu_frame_call(ts, (PyFunctionObject *)kf, kargs, nk, NULL);
    ERROR_IF(callee == NULL);
    callee->ret = kret;
    goto push_callee;
}

push_callee:
    /* Enters callee, pushed and bound; frame->stackpointer is saved. */
    if (!lu_enter_frame(ts, callee, &result)) {
        ERROR_IF(result == NULL);
        PUSH_RETURNED(kret, result);
        POLL();
        DISPATCH();
    }
    callee->previous = frame;
    ts->current_frame = (struct _PyInterpreterFrame *)callee;
    frame = callee;
    LOAD_FRAME();
    DISPATCH();

op_CALL_FUNCTION_EX: {
    /* func, null, callargs, kwargs or NULL */
    PyObject *callargs = sp[-2];
    SAVE_FRAME();
    if (!PyTuple_CheckExact(callargs)) {
        PyObject *t = lu_args_as_tuple(sp[-4], callargs);
        ERROR_IF(t == NULL);
        Py_SETREF(sp[-2], t);
        callargs = t;
    }
    args = lu_unpack_call(callargs, sp[-1], &npos, &kwnames);
    ERROR_IF(args == NULL);
    total = npos + (kwnames != NULL ? PyTuple_GET_SIZE(kwnames) : 0);
    callable = sp[-4];
    Py_XDECREF(sp[-1]);
    Py_DECREF(callargs);
    base = sp = sp - 4;
    to_free = args - 1;
    free_self = 1;
    goto call_tail;
}

op_RETURN_VALUE: {
    PyObject *retval = POP();
    frame->stackpointer = sp;
    struct lu_frame *caller = frame->previous;
    uint8_t ret = frame->ret;
    lu_frame_pop(ts, frame);
    if (caller->owner == LU_FRAME_C_ENTRY) {
        ts->current_frame = (struct _PyInterpreterFrame *)caller->previous;
        return retval;
    }
    ts->current_frame = (struct _PyInterpreterFrame *)caller;
    frame = caller;
    LOAD_FRAME();
    PUSH_RETURNED(ret, retval);
    DISPATCH();
}

op_MAKE_FUNCTION: {
    PyObject *codeobj = POP();
    SAVE_FRAME();
    PyObject *func = lu_function_new(codeobj, frame->globals);
    Py_DECREF(codeobj);
    ERROR_IF(func == NULL);
    PUSH(func);
    DISPATCH();
}

op_SET_FUNCTION_ATTRIBUTE: {
    PyObject *func = POP();
    PyObject *attr = POP();
    set_function_attribute((PyFunctionObject *)func, oparg, attr);
    PUSH(func);
    DISPATCH();
}

op_BUILD_MAP: {
    SAVE_FRAME();
    PyObject **values = sp - 2 * oparg;
    PyObject *map = PyDict_New();
    for (int i = 0; map != NULL && i < oparg; i++) {
        if (lu_ns_insert(map, values[2 * i], values[2 * i + 1]) < 0) {
            Py_CLEAR(map);
        }
    }
    for (int i = 0; i < 2 * oparg; i++) {
        Py_DECREF(values[i]);
    }
    sp = values;
    ERROR_IF(map == NULL);
    PUSH(map);
    DISPATCH();
}

op_IMPORT_NAME: {
    PyObject *fromlist = POP();
    PyObject *level = POP();
    SAVE_FRAME();
    PyObject *res = import_name(ts, NAME(oparg), fromlist, level);
    Py_DECREF(fromlist);
    Py_DECREF(level);
    ERROR_IF(res == NULL);
    PUSH(res);
    DISPATCH();
}

op_IMPORT_FROM: {
    SAVE_FRAME();
    PyObject *res = import_from(TOP(), NAME(oparg));
    ERROR_IF(res == NULL);
    PUSH(res);
    DISPATCH();
}

op_JUMP_BACKWARD_NO_INTERRUPT:
    next_instr -= oparg;
    DISPATCH();

op_JUMP_FORWARD:
    next_instr += oparg;
    DISPATCH();

op_POP_JUMP_IF_FALSE:
    SKIP_CACHES();
    next_instr += oparg * (POP() == Py_False);
    DISPATCH();

op_POP_JUMP_IF_TRUE:
    SKIP_CACHES();
    next_instr += oparg * (POP() == Py_True);
    DISPATCH();

op_POP_JUMP_IF_NONE: {
    SKIP_CACHES();
    PyObject *v = POP();
    next_instr += oparg * (v == Py_None);
    Py_DECREF(v);
    DISPATCH();
}

op_POP_JUMP_IF_NOT_NONE: {
    SKIP_CACHES();
    PyObject *v = POP();
    next_instr += oparg * (v != Py_None);
    Py_DECREF(v);
    DISPATCH();
}

op_TO_BOOL: {
    SKIP_CACHES();
    PyObject *v = TOP();
    int truth = to_bool(v);
    if (truth < 0) {
        SAVE_FRAME();
        goto error;
    }
    Py_DECREF(v);
    TOP() = truth ? Py_True : Py_False;
    DISPATCH();
}

op_UNARY_NOT:
    TOP() = TOP() == Py_True ? Py_False : Py_True;
    DISPATCH();

op_IS_OP: {
    PyObject *right = POP();
    PyObject *left = TOP();
    TOP() = (left == right) != oparg ? Py_True : Py_False;
    Py_DECREF(left);
    Py_DECREF(right);
    DISPATCH();
}

op_COPY: {
    PyObject *v = Py_NewRef(sp[-oparg]);
    PUSH(v);
    DISPATCH();
}

op_SWAP: {
    PyObject *top = TOP();
    TOP() = sp[-oparg];
    sp[-oparg] = top;
    DISPATCH();
}

op_MAKE_CELL: {
    PyObject *cell = PyCell_New(GETLOCAL(oparg));
    Py_XDECREF(GETLOCAL(oparg));
    GETLOCAL(oparg) = cell;
    ERROR_IF(cell == NULL);
    DISPATCH();
}

op_COPY_FREE_VARS: {
    PyObject **closure = &PyTuple_GET_ITEM(((PyFunctionObject *)frame->func)->func_closure, 0);
    PyObject **free = localsplus + code->co_nlocalsplus - oparg;
    for (int i = 0; i < oparg; i++) {
        free[i] = Py_NewRef(closure[i]);
    }
    DISPATCH();
}

op_LOAD_DEREF: {
    PyObject *v = ((PyCellObject *)GETLOCAL(oparg))->ob_ref;
    if (v == NULL) {
        SAVE_FRAME();
        format_unbound(code, oparg);
        goto error;
    }
    PUSH(Py_NewRef(v));
    DISPATCH();
}

op_STORE_DEREF: {
    PyCellObject *cell = (PyCellObject *)GETLOCAL(oparg);
    PyObject *old = cell->ob_ref;
    cell->ob_ref = POP();
    Py_XDECREF(old);
    DISPATCH();
}

op_DELETE_DEREF: {
    PyCellObject *cell = (PyCellObject *)GETLOCAL(oparg);
    PyObject *old = cell->ob_ref;
    if (old == NULL) {
        SAVE_FRAME();
        format_unbound(code, oparg);
        goto error;
    }
    cell->ob_ref = NULL;
    Py_DECREF(old);
    DISPATCH();
}

op_LOAD_FROM_DICT_OR_DEREF: {
    PyObject *mod_or_class_dict = POP();
    SAVE_FRAME();
    PyObject *name = PyTuple_GET_ITEM(code->co_localsplusnames, oparg);
    PyObject *v = ns_get(mod_or_class_dict, name);
    int failed = v == NULL && PyErr_Occurred();
    if (v != NULL) {
        Py_INCREF(v);
    }
    else if (!failed) {
        v = ((PyCellObject *)GETLOCAL(oparg))->ob_ref;
        if (v == NULL) {
            format_unbound(code, oparg);
        }
        else {
            Py_INCREF(v);
        }
    }
    Py_DECREF(mod_or_class_dict);
    ERROR_IF(v == NULL);
    PUSH(v);
    DISPATCH();
}

op_DELETE_FAST:
    if (GETLOCAL(oparg) == NULL) {
        SAVE_FRAME();
        format_unbound(code, oparg);
        goto error;
    }
    Py_CLEAR(GETLOCAL(oparg));
    DISPATCH();

op_LOAD_FAST_AND_CLEAR:
    PUSH(GETLOCAL(oparg));
    GETLOCAL(oparg) = NULL;
    DISPATCH();

op_STORE_GLOBAL: {
    PyObject *v = POP();
    SAVE_FRAME();
    int err = lu_ns_insert(frame->globals, NAME(oparg), v);
    Py_DECREF(v);
    ERROR_IF(err < 0);
    DISPATCH();
}

op_DELETE_GLOBAL:
    SAVE_FRAME();
    if (!lu_ns_delete(frame->globals, NAME(oparg))) {
        name_error(NAME(oparg));
        goto error;
    }
    DISPATCH();

op_DELETE_NAME:
    SAVE_FRAME();
    if (frame->locals == NULL) {
        lu_err_format(PyExc_SystemError, "no locals when deleting '%s'",
                      PyUnicode_AsUTF8(NAME(oparg)));
        goto error;
    }
    if (!PyDict_CheckExact(frame->locals)) {
        lu_err_format(PyExc_SystemError, "namespace is a %s, not a dict",
                      Py_TYPE(frame->locals)->tp_name);
        goto error;
    }
    if (!lu_ns_delete(frame->locals, NAME(oparg))) {
        name_error(NAME(oparg));
        goto error;
    }
    DISPATCH();

op_BUILD_TUPLE: {
    PyObject *t = PyTuple_New(oparg);
    ERROR_IF(t == NULL);
    sp -= oparg;
    for (int i = 0; i < oparg; i++) {
        PyTuple_SET_ITEM(t, i, sp[i]);
    }
    PUSH(t);
    DISPATCH();
}

op_BUILD_LIST: {
    PyObject *l = PyList_New(oparg);
    ERROR_IF(l == NULL);
    sp -= oparg;
    for (int i = 0; i < oparg; i++) {
        PyList_SET_ITEM(l, i, sp[i]);
    }
    PUSH(l);
    DISPATCH();
}

op_LIST_APPEND: {
    PyObject *v = POP();
    int err = lu_list_append(sp[-oparg], v);
    Py_DECREF(v);
    ERROR_IF(err < 0);
    DISPATCH();
}

op_LIST_EXTEND: {
    /* Tuples and lists until iteration exists (K1h). */
    PyObject *iterable = POP();
    PyObject *list = sp[-oparg];
    SAVE_FRAME();
    int err = 0;
    if (PyTuple_Check(iterable) || PyList_Check(iterable)) {
        Py_ssize_t n = PyTuple_Check(iterable) ? PyTuple_GET_SIZE(iterable)
                                               : PyList_GET_SIZE(iterable);
        for (Py_ssize_t i = 0; err == 0 && i < n; i++) {
            err = lu_list_append(list, PyTuple_Check(iterable) ? PyTuple_GET_ITEM(iterable, i)
                                                              : PyList_GET_ITEM(iterable, i));
        }
    }
    else {
        lu_err_format(PyExc_TypeError, "Value after * must be an iterable, not %.200s",
                      Py_TYPE(iterable)->tp_name);
        err = -1;
    }
    Py_DECREF(iterable);
    ERROR_IF(err < 0);
    DISPATCH();
}

op_DICT_MERGE: {
    PyObject *update = POP();
    SAVE_FRAME();
    int err = lu_dict_merge(sp[-oparg - 3], sp[-oparg], update);
    Py_DECREF(update);
    ERROR_IF(err < 0);
    DISPATCH();
}

op_DICT_UPDATE: {
    PyObject *update = POP();
    PyObject *dict = sp[-oparg];
    SAVE_FRAME();
    int err = 0;
    if (!PyDict_CheckExact(update)) {
        lu_err_format(PyExc_TypeError, "'%.200s' object is not a mapping", Py_TYPE(update)->tp_name);
        err = -1;
    }
    else {
        Py_ssize_t pos = 0;
        PyObject *key, *value;
        while (err == 0 && lu_ns_next(update, &pos, &key, &value)) {
            err = lu_ns_insert(dict, key, value);
        }
    }
    Py_DECREF(update);
    ERROR_IF(err < 0);
    DISPATCH();
}

op_CALL_INTRINSIC_1: {
    /* Only INTRINSIC_LIST_TO_TUPLE until the other intrinsics have users. */
    SAVE_FRAME();
    if (oparg != INTRINSIC_LIST_TO_TUPLE) {
        lu_err_format(PyExc_SystemError, "intrinsic %d arrives with its consumer", oparg);
        goto error;
    }
    PyObject *list = TOP();
    PyObject *t = lu_args_as_tuple(Py_None, list);
    ERROR_IF(t == NULL);
    Py_DECREF(list);
    TOP() = t;
    DISPATCH();
}

op_JUMP_BACKWARD:
    SKIP_CACHES();
    next_instr -= oparg;
    POLL();
    DISPATCH();

op_LOAD_ATTR: {
    /* owner -- attr, self_or_null[oparg & 1]; the method form pushes a NULL
       self and CALL unpacks the bound method. */
    PyObject *name = NAME(oparg >> 1);
    PyObject *owner = TOP();
    SKIP_CACHES();
    kret = (oparg & 1) ? LU_RETURN_METHOD : LU_RETURN_PUSH;
    if (PyModule_CheckExact(owner)) {
        PyObject *v = lu_ns_lookup(((PyModuleObject *)owner)->md_dict, name);
        if (v != NULL) {
            TOP() = Py_NewRef(v);
            Py_DECREF(owner);
            if (oparg & 1) {
                PUSH(NULL);
            }
            DISPATCH();
        }
    }
    getattrofunc get = Py_TYPE(owner)->tp_getattro;
    if (get == lu_slot_tp_getattro || get == PyObject_GenericGetAttr) {
        kid = get == lu_slot_tp_getattro ? LU_K_getattr : LU_K_generic_getattr;
        kargs[0] = POP();
        kargs[1] = Py_NewRef(name);
        nk = 2;
        goto kernel_call;
    }
    SAVE_FRAME();
    struct lu_task *t = (struct lu_task *)ts;
    t->pin++;
    PyObject *v = PyObject_GetAttr(owner, name);
    t->pin--;
    ERROR_IF(v == NULL);
    TOP() = v;
    Py_DECREF(owner);
    if (oparg & 1) {
        PUSH(NULL);
    }
    DISPATCH();
}

op_STORE_ATTR:
op_DELETE_ATTR: {
    /* v, owner -- (STORE_ATTR); owner -- (DELETE_ATTR) */
    PyObject *name = NAME(oparg);
    int store = opcode == STORE_ATTR;
    PyObject *owner = sp[-1];
    PyObject *v = store ? sp[-2] : NULL;
    SKIP_CACHES();
    setattrofunc set = Py_TYPE(owner)->tp_setattro;
    if (set == lu_slot_tp_setattro || set == PyObject_GenericSetAttr) {
        sp -= store ? 2 : 1;
        kargs[0] = owner;
        kargs[1] = Py_NewRef(name);
        kargs[2] = v;
        nk = store ? 3 : 2;
        kid = set == PyObject_GenericSetAttr ? LU_K_generic_setattr : store ? LU_K_setattr : LU_K_delattr;
        if (kid == LU_K_generic_setattr && !store) {
            kargs[2] = Py_NewRef(LU_MISSING);
            nk = 3;
        }
        kret = LU_RETURN_DISCARD;
        goto kernel_call;
    }
    SAVE_FRAME();
    struct lu_task *t = (struct lu_task *)ts;
    t->pin++;
    int err = PyObject_SetAttr(owner, name, v);
    t->pin--;
    Py_DECREF(owner);
    Py_XDECREF(v);
    sp -= store ? 2 : 1;
    ERROR_IF(err < 0);
    DISPATCH();
}

op_LOAD_SUPER_ATTR: {
    /* global_super, class, self -- attr, self_or_null[oparg & 1] */
    PyObject *name = NAME(oparg >> 2);
    SKIP_CACHES();
    kret = (oparg & 1) ? LU_RETURN_METHOD : LU_RETURN_PUSH;
    if (sp[-3] == (PyObject *)&PySuper_Type && PyType_Check(sp[-2])) {
        kargs[2] = Py_NewRef(name);
        kargs[1] = POP();
        kargs[0] = POP();
        Py_DECREF(POP());
        kid = LU_K_super_getattr;
        nk = 3;
        goto kernel_call;
    }
    /* super is shadowed: call it, then take the attribute, from C. */
    SAVE_FRAME();
    struct lu_task *t = (struct lu_task *)ts;
    t->pin++;
    PyObject *sup = lu_call(sp[-3], sp - 2, oparg & 2, NULL);
    PyObject *v = sup != NULL ? PyObject_GetAttr(sup, name) : NULL;
    t->pin--;
    Py_XDECREF(sup);
    for (int i = 0; i < 3; i++) {
        Py_DECREF(POP());
    }
    ERROR_IF(v == NULL);
    PUSH(v);
    if (oparg & 1) {
        PUSH(NULL);
    }
    DISPATCH();
}

op_LOAD_BUILD_CLASS: {
    PyObject *bc = ns_get(frame->builtins, LU_ID(__build_class__));
    if (bc == NULL) {
        SAVE_FRAME();
        if (!PyErr_Occurred()) {
            lu_err_format(PyExc_NameError, "__build_class__ not found");
        }
        goto error;
    }
    PUSH(Py_NewRef(bc));
    DISPATCH();
}

op_LOAD_LOCALS:
    if (frame->locals == NULL) {
        SAVE_FRAME();
        lu_err_format(PyExc_SystemError, "no locals found");
        goto error;
    }
    PUSH(Py_NewRef(frame->locals));
    DISPATCH();

op_LOAD_FROM_DICT_OR_GLOBALS: {
    PyObject *dict = POP();
    PyObject *name = NAME(oparg);
    SAVE_FRAME();
    PyObject *v;
    if ((v = ns_get(dict, name)) == NULL && !PyErr_Occurred()
        && (v = ns_get(frame->globals, name)) == NULL && !PyErr_Occurred()
        && (v = ns_get(frame->builtins, name)) == NULL && !PyErr_Occurred()) {
        name_error(name);
    }
    Py_XINCREF(v);
    Py_DECREF(dict);
    ERROR_IF(v == NULL);
    PUSH(v);
    DISPATCH();
}

op_SETUP_ANNOTATIONS: {
    SAVE_FRAME();
    PyObject *name = PyUnicode_InternFromString("__annotations__");
    int err = name == NULL;
    if (!err && frame->locals == NULL) {
        lu_err_format(PyExc_SystemError, "no locals found when setting up annotations");
        err = 1;
    }
    if (!err && ns_get(frame->locals, name) == NULL) {
        PyObject *d = PyErr_Occurred() ? NULL : PyDict_New();
        err = d == NULL || lu_ns_insert(frame->locals, name, d) < 0;
        Py_XDECREF(d);
    }
    Py_XDECREF(name);
    ERROR_IF(err);
    DISPATCH();
}

op_GET_ITER: {
    PyObject *iterable = TOP();
    SAVE_FRAME();
    PyObject *it = get_iter(iterable);
    ERROR_IF(it == NULL);
    TOP() = it;
    Py_DECREF(iterable);
    DISPATCH();
}

op_FOR_ITER: {
    /* iter -- iter, next; exhausted, jumps past the END_FOR at the target */
    SKIP_CACHES();
    SAVE_FRAME();
    PyObject *v = iter_next(TOP());
    if (v == NULL) {
        ERROR_IF(PyErr_Occurred());
        next_instr += oparg + 1;
        DISPATCH();
    }
    PUSH(v);
    DISPATCH();
}

op_COMPARE_OP: {
    /* Exact str equality and compact ints; rich comparison is K1d's. */
    SKIP_CACHES();
    PyObject *right = POP();
    PyObject *left = TOP();
    int op = oparg >> 5, r;
    if (PyUnicode_CheckExact(left) && PyUnicode_CheckExact(right) && (op == Py_EQ || op == Py_NE)) {
        r = lu_str_eq(left, right) == (op == Py_EQ);
    }
    else if (PyLong_CheckExact(left) && PyLong_CheckExact(right)
             && _PyLong_IsCompact((PyLongObject *)left) && _PyLong_IsCompact((PyLongObject *)right)) {
        Py_ssize_t a = _PyLong_CompactValue((PyLongObject *)left);
        Py_ssize_t b = _PyLong_CompactValue((PyLongObject *)right);
        r = op == Py_LT ? a < b : op == Py_LE ? a <= b : op == Py_EQ ? a == b
          : op == Py_NE ? a != b : op == Py_GT ? a > b : a >= b;
    }
    else {
        SAVE_FRAME();
        lu_err_format(PyExc_SystemError, "comparing '%s' and '%s' arrives with K1d",
                      Py_TYPE(left)->tp_name, Py_TYPE(right)->tp_name);
        Py_DECREF(right);
        goto error;
    }
    TOP() = r ? Py_True : Py_False;
    Py_DECREF(left);
    Py_DECREF(right);
    DISPATCH();
}

op_UNPACK_SEQUENCE: {
    SKIP_CACHES();
    PyObject *seq = POP();
    SAVE_FRAME();
    int err = unpack(seq, oparg, sp);
    Py_DECREF(seq);
    if (err < 0) {
        frame->stackpointer = sp;
        goto error;
    }
    sp += oparg;
    DISPATCH();
}

op_RAISE_VARARGS: {
    /* raise, raise exc, raise exc from cause */
    SAVE_FRAME();
    if (oparg == 0) {
        PyObject *exc = lu_topmost_exc_info(ts)->exc_value;
        if (exc == NULL || exc == Py_None) {
            lu_err_format(PyExc_RuntimeError, "No active exception to reraise");
            goto error;
        }
        PyErr_SetRaisedException(Py_NewRef(exc));
        lasti = INSTR_START() - lu_code_start(code);
        goto exception_unwind;
    }
    sp -= oparg;
    kargs[0] = sp[0];
    kargs[1] = oparg == 2 ? sp[1] : Py_NewRef(LU_MISSING);
    kid = LU_K_do_raise;
    nk = 2;
    kret = LU_RETURN_DISCARD;
    goto kernel_call;
}

op_RERAISE: {
    PyObject *exc = POP();
    long saved;
    lasti = oparg && lu_long_as_long(sp[-oparg], &saved) == 0
        ? saved : INSTR_START() - lu_code_start(code);
    PyErr_SetRaisedException(exc);
    goto exception_unwind;
}

op_PUSH_EXC_INFO: {
    /* exc -- prev_exc, exc */
    PyObject *exc = TOP();
    _PyErr_StackItem *info = ts->exc_info;
    PyObject *prev = info->exc_value;
    info->exc_value = Py_NewRef(exc);
    TOP() = prev != NULL ? prev : Py_NewRef(Py_None);
    PUSH(exc);
    DISPATCH();
}

op_POP_EXCEPT: {
    PyObject *prev = POP();
    _PyErr_StackItem *info = ts->exc_info;
    PyObject *old = info->exc_value;
    if (prev == Py_None) {
        Py_DECREF(prev);
        prev = NULL;
    }
    info->exc_value = prev;
    Py_XDECREF(old);
    DISPATCH();
}

op_CHECK_EXC_MATCH: {
    /* left, right -- left, left matches right */
    PyObject *right = POP();
    int ok = check_except_type(right);
    int match = ok == 0 && PyErr_GivenExceptionMatches(TOP(), right);
    Py_DECREF(right);
    if (ok < 0) {
        SAVE_FRAME();
        goto error;
    }
    PUSH(match ? Py_True : Py_False);
    DISPATCH();
}

op_CHECK_EG_MATCH: {
    /* exc_value, match_type -- rest, match */
    SAVE_FRAME();
    kargs[1] = POP();
    kargs[0] = POP();
    kid = LU_K_exc_group_match;
    nk = 2;
    kret = LU_RETURN_PAIR;
    goto kernel_call;
}

op_CALL_INTRINSIC_2: {
    /* Only INTRINSIC_PREP_RERAISE_STAR until the other intrinsics have users. */
    SAVE_FRAME();
    if (oparg != INTRINSIC_PREP_RERAISE_STAR) {
        lu_err_format(PyExc_SystemError, "intrinsic %d arrives with its consumer", oparg);
        goto error;
    }
    kargs[1] = POP();
    kargs[0] = POP();
    kid = LU_K_prep_reraise_star;
    nk = 2;
    kret = LU_RETURN_PUSH;
    goto kernel_call;
}

op_LOAD_SPECIAL: {
    /* owner -- attr, self_or_null; looked up on the type, never the instance */
    SAVE_FRAME();
    kargs[0] = POP();
    kargs[1] = lu_small_int(oparg);
    kid = LU_K_load_special;
    nk = 2;
    kret = LU_RETURN_METHOD;
    goto kernel_call;
}

op_WITH_EXCEPT_START: {
    /* exit_func, exit_self, lasti, prev_exc, val -- ..., res: calls
       exit_func([exit_self,] type(val), val, val.__traceback__) as a CALL
       whose operands stay on the stack. */
    PyObject *val = TOP();
    PyObject *tb = ((PyBaseExceptionObject *)val)->traceback;
    PyObject *exit_self = sp[-4];
    exit_args[0] = NULL;
    exit_args[1] = exit_self;
    exit_args[2] = (PyObject *)Py_TYPE(val);
    exit_args[3] = val;
    exit_args[4] = tb ? tb : Py_None;
    SAVE_FRAME();
    callable = Py_NewRef(sp[-5]);
    args = exit_self != NULL ? exit_args + 1 : exit_args + 2;
    npos = total = exit_self != NULL ? 4 : 3;
    for (Py_ssize_t i = 0; i < total; i++) {
        Py_INCREF(args[i]);
    }
    kwnames = NULL;
    base = sp;
    free_self = exit_self == NULL;
    to_free = NULL;
    goto call_tail;
}

op_LOAD_COMMON_CONSTANT: {
    /* _common_constants of Lib/opcode.py */
    static const char *const names[] = { NULL, NULL, "tuple", "all", "any" };
    PyObject *v = oparg == 0 ? PyExc_AssertionError : oparg == 1 ? PyExc_NotImplementedError : NULL;
    if (v == NULL) {
        SAVE_FRAME();
        PyObject *name = oparg < 5 ? PyUnicode_InternFromString(names[oparg]) : NULL;
        v = name != NULL ? lu_ns_lookup(ts->interp->builtins, name) : NULL;
        Py_XDECREF(name);
        if (v == NULL) {
            lu_err_format(PyExc_SystemError, "common constant %d is not defined yet", oparg);
            goto error;
        }
    }
    PUSH(Py_NewRef(v));
    DISPATCH();
}

error:
    /* The raising frame joins the traceback; kernel frames look native and
       do not. */
    lasti = INSTR_START() - lu_code_start(code);
    if (frame->owner != LU_FRAME_KERNEL_HIDDEN) {
        lu_traceback_here(frame, lasti);
    }
exception_unwind: {
    /* The handler of the instruction the loop is at (RERAISE and a bare
       raise arrive here without a new traceback entry), or the caller's
       turn. */
    int level, handler, push_lasti;
    int offset = (int)(next_instr - lu_code_start(code)) - 1;
    if (lu_exception_handler(code, offset, &level, &handler, &push_lasti)) {
        PyObject **new_top = localsplus + code->co_nlocalsplus + level;
        while (sp > new_top) {
            Py_XDECREF(*--sp);
        }
        if (push_lasti) {
            PUSH(lasti <= 256 ? lu_small_int((int)lasti) : PyLong_FromLong((long)lasti));
        }
        PUSH(PyErr_GetRaisedException());
        next_instr = lu_code_start(code) + handler;
        DISPATCH();
    }
    frame->stackpointer = sp;
    struct lu_frame *caller = frame->previous;
    lu_frame_pop(ts, frame);
    if (caller->owner == LU_FRAME_C_ENTRY) {
        ts->current_frame = (struct _PyInterpreterFrame *)caller->previous;
        return NULL;
    }
    ts->current_frame = (struct _PyInterpreterFrame *)caller;
    frame = caller;
    LOAD_FRAME();
    goto error;
}
}

PyObject *
_PyEval_EvalFrameDefault(PyThreadState *ts, struct _PyInterpreterFrame *f, int throwflag)
{
    if (throwflag) {
        lu_err_format(PyExc_SystemError, "throwing into a frame arrives with K1l");
        lu_frame_pop(ts, (struct lu_frame *)f);
        return NULL;
    }
    return lu_eval(ts, (struct lu_frame *)f);
}

PyObject *
PyEval_EvalCode(PyObject *co, PyObject *globals, PyObject *locals)
{
    PyThreadState *ts = PyThreadState_Get();
    if (lu_ns_lookup(globals, LU_ID(__builtins__)) == NULL
        && lu_ns_insert(globals, LU_ID(__builtins__), ts->interp->builtins) < 0) {
        return NULL;
    }
    PyObject *func = lu_function_new(co, globals);
    if (func == NULL) {
        return NULL;
    }
    struct lu_frame *f = lu_frame_push(ts, (PyFunctionObject *)func, locals);
    Py_DECREF(func);
    if (f == NULL) {
        return NULL;
    }
    PyObject *result;
    if (lu_enter_frame(ts, f, &result)) {
        result = lu_eval(ts, f);
    }
    return result;
}
