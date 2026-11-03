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
#include "interp/gen.h"

#include "opcode.h"
#include "opcode_ids.h"
#include "prim/prim.h"

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

#define INTRINSIC_UNARY_POSITIVE 5  /* pycore_intrinsics.h */
#define INTRINSIC_PREP_RERAISE_STAR 1   /* pycore_intrinsics.h */
#define INTRINSIC_LIST_TO_TUPLE 6
#define INTRINSIC_STOPITERATION_ERROR 3
#define INTRINSIC_ASYNC_GEN_WRAP 4

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

/* PyObject_IsTrue where truth is a layout: 1 or 0, or -1 when the type's
   __bool__ or __len__ must run (kernel truth). */
int
lu_truth_exact(PyObject *v)
{
    if (v == Py_True || v == Py_False) {
        return v == Py_True;
    }
    if (v == Py_None) {
        return 0;
    }
    if (PyLong_CheckExact(v)) {
        return ((PyLongObject *)v)->long_value.lv_tag >= (1 << _PyLong_NON_SIZE_BITS);
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
    return -1;
}

/* PyObject_IsTrue for C callers: the kernel runs __bool__ and __len__. */
int
lu_is_true(PyObject *v)
{
    int t = lu_truth_exact(v);
    if (t >= 0) {
        return t;
    }
    PyObject *r = lu_call_kernel(LU_K_truth, &v, 1);
    if (r == NULL) {
        return -1;
    }
    t = r == Py_True;
    Py_DECREF(r);
    return t;
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

/* IMPORT_NAME: builtins.__import__(name, globals, locals, fromlist, level).
   Until builtins.py has defined it, while the kernel boots, a name is its
   sys.modules entry. */
static PyObject *
import_name(PyThreadState *ts, struct lu_frame *frame, PyObject *name, PyObject *fromlist,
            PyObject *level)
{
    PyObject *import_func = lu_ns_lookup(ts->interp->builtins, LU_ID(__import__));
    if (import_func == NULL) {
        PyObject *mod = lu_ns_lookup(ts->interp->modules, name);
        if (mod == NULL) {
            lu_err_format(PyExc_ModuleNotFoundError, "No module named '%s'", PyUnicode_AsUTF8(name));
        }
        return Py_XNewRef(mod);
    }
    PyObject *args[5] = { name, frame->globals, frame->locals != NULL ? frame->locals : Py_None,
                          fromlist, level };
    return lu_call(import_func, args, 5, NULL);
}

/* IMPORT_FROM: the attribute of an exact module's dict, else the kernel's
   import_from, which falls back to sys.modules and words the error. */
static PyObject *
import_from(PyObject *module, PyObject *name)
{
    PyObject *v = PyModule_CheckExact(module)
        ? lu_ns_lookup(((PyModuleObject *)module)->md_dict, name) : NULL;
    if (v != NULL) {
        return Py_NewRef(v);
    }
    PyObject *args[2] = { module, name };
    return lu_call_kernel(LU_K_import_from, args, 2);
}

/* GET_ITER of the exact types the loop iterates itself; NULL without an
   error for every other iterable, which the kernel's get_iter handles. */
static PyObject *
get_iter_exact(PyObject *v)
{
    PyTypeObject *tp = Py_TYPE(v);
    if (tp == &PyTuple_Type || tp == &PyList_Type) {
        seqiterobject *it = (seqiterobject *)lu_object_new(
            tp == &PyTuple_Type ? &PyTupleIter_Type : &PyListIter_Type, sizeof(seqiterobject));
        if (it != NULL) {
            it->it_seq = Py_NewRef(v);
            return (PyObject *)it;
        }
        return NULL;
    }
    if (tp == &PyRange_Type) {
        return lu_range_iter(v);
    }
    return NULL;
}

/* UNPACK_SEQUENCE of an exact tuple or list of n items: the items, last
   first. Other sizes and iterables go to the kernel for its messages. */
static int
unpack_exact(PyObject *seq, int n, PyObject **out)
{
    if ((!PyTuple_CheckExact(seq) && !PyList_CheckExact(seq)) || Py_SIZE(seq) != n) {
        return 0;
    }
    PyObject **items = PyTuple_CheckExact(seq) ? &PyTuple_GET_ITEM(seq, 0) : ((PyListObject *)seq)->ob_item;
    for (int i = 0; i < n; i++) {
        out[n - 1 - i] = Py_NewRef(items[i]);
    }
    return 1;
}

/* The oparg of the instruction at `at`, with its EXTENDED_ARG prefixes. */
static int
instr_oparg(lu_codeunit *at)
{
    int arg = at->op.arg, shift = 8;
    for (lu_codeunit *p = at - 1; p->op.code == EXTENDED_ARG; p--, shift += 8) {
        arg |= p->op.arg << shift;
    }
    return arg;
}

/* BINARY_OP NB_SUBSCR and STORE_SUBSCR on an exact list or tuple with a
   compact int index in range: the item's address, or NULL. */
static PyObject **
subscr_slot(PyObject *container, PyObject *sub)
{
    if (!PyLong_CheckExact(sub) || !_PyLong_IsCompact((PyLongObject *)sub)
        || _PyLong_CompactValue((PyLongObject *)sub) < 0) {
        return NULL;
    }
    Py_ssize_t i = (Py_ssize_t)_PyLong_CompactValue((PyLongObject *)sub);
    if (PyList_CheckExact(container) && i < Py_SIZE(container)
        && _Py_IsOwnedByCurrentThread(container)) {
        return &((PyListObject *)container)->ob_item[i];
    }
    if (PyTuple_CheckExact(container) && i < Py_SIZE(container)) {
        return &((PyTupleObject *)container)->ob_item[i];
    }
    return NULL;
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

/* Runs `frame` in a new activation; with throwflag, by raising the
   exception in flight at its current instruction (a generator's throw). */
static PyObject *
eval_frame(PyThreadState *ts, struct lu_frame *frame, int throwflag)
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
        [BUILD_SET] = &&op_BUILD_SET,
        [SET_ADD] = &&op_SET_ADD,
        [SET_UPDATE] = &&op_SET_UPDATE,
        [MAP_ADD] = &&op_MAP_ADD,
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
        [BINARY_OP] = &&op_BINARY_OP,
        [UNARY_NEGATIVE] = &&op_UNARY_NEGATIVE,
        [UNARY_INVERT] = &&op_UNARY_INVERT,
        [CONTAINS_OP] = &&op_CONTAINS_OP,
        [RAISE_VARARGS] = &&op_RAISE_VARARGS,
        [RERAISE] = &&op_RERAISE,
        [PUSH_EXC_INFO] = &&op_PUSH_EXC_INFO,
        [POP_EXCEPT] = &&op_POP_EXCEPT,
        [CHECK_EXC_MATCH] = &&op_CHECK_EXC_MATCH,
        [CHECK_EG_MATCH] = &&op_CHECK_EG_MATCH,
        [WITH_EXCEPT_START] = &&op_WITH_EXCEPT_START,
        [LOAD_SPECIAL] = &&op_LOAD_SPECIAL,
        [LOAD_COMMON_CONSTANT] = &&op_LOAD_COMMON_CONSTANT,
        [UNPACK_EX] = &&op_UNPACK_EX,
        [STORE_SUBSCR] = &&op_STORE_SUBSCR,
        [DELETE_SUBSCR] = &&op_DELETE_SUBSCR,
        [BUILD_SLICE] = &&op_BUILD_SLICE,
        [BINARY_SLICE] = &&op_BINARY_SLICE,
        [STORE_SLICE] = &&op_STORE_SLICE,
        [GET_LEN] = &&op_GET_LEN,
        [CALL_INTRINSIC_2] = &&op_CALL_INTRINSIC_2,
        [FORMAT_SIMPLE] = &&op_FORMAT_SIMPLE,
        [FORMAT_WITH_SPEC] = &&op_FORMAT_WITH_SPEC,
        [CONVERT_VALUE] = &&op_CONVERT_VALUE,
        [BUILD_STRING] = &&op_BUILD_STRING,
        [RETURN_GENERATOR] = &&op_RETURN_GENERATOR,
        [YIELD_VALUE] = &&op_YIELD_VALUE,
        [SEND] = &&op_SEND,
        [END_SEND] = &&op_END_SEND,
        [GET_YIELD_FROM_ITER] = &&op_GET_YIELD_FROM_ITER,
        [GET_AWAITABLE] = &&op_GET_AWAITABLE,
        [GET_AITER] = &&op_GET_AITER,
        [GET_ANEXT] = &&op_GET_ANEXT,
        [END_ASYNC_FOR] = &&op_END_ASYNC_FOR,
        [CLEANUP_THROW] = &&op_CLEANUP_THROW,
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
    PyObject *retval;           /* what a returning or yielding frame hands its caller */
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
        else if ((ret) == LU_RETURN_UNPACK) { \
            for (Py_ssize_t i_ = PyTuple_GET_SIZE(v); --i_ >= 0; ) { \
                PUSH(Py_NewRef(PyTuple_GET_ITEM(v, i_))); \
            } \
            Py_DECREF(v); \
        } \
        else if ((ret) == LU_RETURN_SENT) { \
            PUSH(Py_NewRef(PyTuple_GET_ITEM(v, 0))); \
            if (PyTuple_GET_ITEM(v, 1) == Py_True) { \
                next_instr += instr_oparg(INSTR_START()); \
            } \
            Py_DECREF(v); \
        } \
        else if ((ret) == LU_RETURN_FOR_ITER) { \
            if ((v) == LU_MISSING) { \
                next_instr += instr_oparg(INSTR_START()) + 1; \
            } \
            else { \
                PUSH(v); \
            } \
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
    if (throwflag) {
        goto error;
    }
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
        if (callee->owner == LU_FRAME_GENERATOR) {
            goto resume_linked;
        }
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

resume_linked:
    /* A generator's frame, linked above this one by its resumption; a throw
       enters it by raising. */
    ts->current_frame = (struct _PyInterpreterFrame *)callee;
    frame = callee;
    LOAD_FRAME();
    ERROR_IF(ts->current_exception != NULL);
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

op_RETURN_VALUE:
    retval = POP();
    frame->stackpointer = sp;
return_frame: {
    struct lu_frame *caller = frame->previous;
    uint8_t ret = frame->ret;
    if (frame->owner == LU_FRAME_GENERATOR) {
        lu_gen_finish(ts, frame);
        if (ret == LU_RETURN_FOR_ITER) {
            Py_DECREF(retval);
            retval = LU_MISSING;
        }
    }
    else {
        lu_frame_pop(ts, frame);
    }
    if (caller->owner == LU_FRAME_C_ENTRY) {
        ts->current_frame = (struct _PyInterpreterFrame *)caller->previous;
        return retval;
    }
    ts->current_frame = (struct _PyInterpreterFrame *)caller;
    frame = caller;
    LOAD_FRAME();
    if (ret == LU_RETURN_SEND) {
        next_instr += instr_oparg(INSTR_START());       /* out of the yield-from loop */
    }
    PUSH_RETURNED(ret, retval);
    DISPATCH();
}

op_RETURN_GENERATOR:
    /* The function's frame moves into a new generator, which it returns. */
    SAVE_FRAME();
    retval = lu_gen_new(ts, frame);
    ERROR_IF(retval == NULL);
    goto return_frame;

op_YIELD_VALUE: {
    retval = POP();
    SAVE_FRAME();
    struct lu_frame *caller = frame->previous;
    uint8_t ret = frame->ret;
    lu_gen_suspend(ts, frame, oparg);
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

op_SEND: {
    /* receiver, v -- receiver, retval; when the receiver returns, jumps by
       oparg past the yield-from loop. Generators and coroutines run in
       this loop; other receivers are the kernel's. */
    SKIP_CACHES();
    PyObject *receiver = sp[-2];
    if ((Py_IS_TYPE(receiver, &PyGen_Type) || Py_IS_TYPE(receiver, &PyCoro_Type))
        && ts->interp->eval_frame == NULL) {
        PyObject *v = POP();
        SAVE_FRAME();
        callee = lu_gen_resume(ts, receiver, v);
        if (callee == NULL) {
            ERROR_IF(PyErr_Occurred());
            PUSH(Py_NewRef(Py_None));
            next_instr += oparg;
            DISPATCH();
        }
        callee->ret = LU_RETURN_SEND;
        goto resume_linked;
    }
    kargs[1] = POP();
    kargs[0] = Py_NewRef(receiver);
    nk = 2;
    kid = LU_K_send;
    kret = LU_RETURN_SENT;
    goto kernel_call;
}

op_END_SEND: {
    /* receiver, value -- value */
    PyObject *v = POP();
    Py_DECREF(TOP());
    TOP() = v;
    DISPATCH();
}

op_GET_YIELD_FROM_ITER: {
    PyObject *v = TOP();
    if (Py_IS_TYPE(v, &PyCoro_Type)) {
        if (!(code->co_flags & (CO_COROUTINE | CO_ITERABLE_COROUTINE))) {
            SAVE_FRAME();
            lu_err_format(PyExc_TypeError, "cannot 'yield from' a coroutine object "
                          "in a non-coroutine generator");
            goto error;
        }
        DISPATCH();
    }
    goto op_GET_ITER;
}

op_GET_AWAITABLE: {
    PyObject *v = TOP();
    if (Py_IS_TYPE(v, &PyCoro_Type)) {
        if (((struct lu_gen *)v)->frame_state == LU_GEN_SUSPENDED_YIELD_FROM) {
            SAVE_FRAME();
            lu_err_format(PyExc_RuntimeError, "coroutine is being awaited already");
            goto error;
        }
        DISPATCH();
    }
    kargs[0] = POP();
    kargs[1] = lu_small_int(oparg);
    nk = 2;
    kid = LU_K_get_awaitable;
    kret = LU_RETURN_PUSH;
    goto kernel_call;
}

op_GET_AITER:
    kargs[0] = POP();
    nk = 1;
    kid = LU_K_get_aiter;
    kret = LU_RETURN_PUSH;
    goto kernel_call;

op_GET_ANEXT:
    /* aiter -- aiter, awaitable */
    kargs[0] = Py_NewRef(TOP());
    nk = 1;
    kid = LU_K_get_anext;
    kret = LU_RETURN_PUSH;
    goto kernel_call;

op_END_ASYNC_FOR:
    /* awaitable, exc -- ; StopAsyncIteration ends the loop, anything else
       propagates */
    if (PyErr_GivenExceptionMatches(TOP(), PyExc_StopAsyncIteration)) {
        Py_DECREF(POP());
        Py_DECREF(POP());
        DISPATCH();
    }
    SAVE_FRAME();
    PyErr_SetRaisedException(POP());
    lasti = INSTR_START() - lu_code_start(code);
    goto exception_unwind;

op_CLEANUP_THROW: {
    /* sub_iter, last_sent_val, exc -- None, value: a throw or close that
       ended the delegate with StopIteration gives `yield from` its value */
    PyObject *exc = TOP();
    if (PyErr_GivenExceptionMatches(exc, PyExc_StopIteration)) {
        PyObject *value = ((PyStopIterationObject *)exc)->value;
        value = Py_NewRef(value != NULL ? value : Py_None);
        for (int i = 0; i < 3; i++) {
            Py_DECREF(POP());
        }
        PUSH(Py_NewRef(Py_None));
        PUSH(value);
        DISPATCH();
    }
    SAVE_FRAME();
    PyErr_SetRaisedException(POP());
    lasti = INSTR_START() - lu_code_start(code);
    goto exception_unwind;
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

/* The oparg items below the stack top as one tuple, for a kernel builder. */
build_items: {
    PyObject **values = sp - oparg;
    PyObject *items = PyTuple_New(oparg);
    if (items == NULL) {
        for (int i = 0; i < oparg; i++) {
            Py_DECREF(values[i]);
        }
        sp = values;
        goto error;
    }
    for (int i = 0; i < oparg; i++) {
        PyTuple_SET_ITEM(items, i, values[i]);
    }
    sp = values;
    kargs[0] = items;
    nk = 1;
    kret = LU_RETURN_PUSH;
    goto kernel_call;
}

op_BUILD_SET:
    SAVE_FRAME();
    kid = LU_K_build_set;
    goto build_items;

op_SET_ADD: {
    PyObject *v = POP();
    SAVE_FRAME();
    kargs[0] = Py_NewRef(sp[-oparg]);
    kargs[1] = v;
    nk = 2;
    kid = LU_K_set_add;
    kret = LU_RETURN_DISCARD;
    goto kernel_call;
}

op_SET_UPDATE: {
    PyObject *iterable = POP();
    SAVE_FRAME();
    kargs[0] = Py_NewRef(sp[-oparg]);
    kargs[1] = iterable;
    nk = 2;
    kid = LU_K_set_update;
    kret = LU_RETURN_DISCARD;
    goto kernel_call;
}

op_MAP_ADD: {
    PyObject *value = POP();
    PyObject *key = POP();
    PyObject *map = sp[-oparg];
    SAVE_FRAME();
    if (PyDict_CheckExact(map) && PyUnicode_CheckExact(key)) {
        int err = lu_ns_insert(map, key, value);
        Py_DECREF(key);
        Py_DECREF(value);
        ERROR_IF(err < 0);
        DISPATCH();
    }
    kargs[0] = Py_NewRef(map);
    kargs[1] = key;
    kargs[2] = value;
    nk = 3;
    kid = LU_K_setitem;
    kret = LU_RETURN_DISCARD;
    goto kernel_call;
}

op_BUILD_MAP: {
    SAVE_FRAME();
    PyObject **values = sp - 2 * oparg;
    for (int i = 0; i < oparg; i++) {
        if (!PyUnicode_CheckExact(values[2 * i])) {
            oparg *= 2;
            kid = LU_K_build_map;
            goto build_items;
        }
    }
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
    PyObject *res = import_name(ts, frame, NAME(oparg), fromlist, level);
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
    int truth = lu_truth_exact(v);
    if (truth < 0) {
        kargs[0] = POP();
        nk = 1;
        kid = LU_K_truth;
        kret = LU_RETURN_PUSH;
        goto kernel_call;
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
    PyObject *iterable = POP();
    PyObject *list = sp[-oparg];
    SAVE_FRAME();
    if (PyTuple_CheckExact(iterable) || PyList_CheckExact(iterable)) {
        Py_ssize_t n = Py_SIZE(iterable);
        int err = 0;
        for (Py_ssize_t i = 0; err == 0 && i < n && i < Py_SIZE(iterable); i++) {
            err = lu_list_append(list, PyTuple_CheckExact(iterable) ? PyTuple_GET_ITEM(iterable, i)
                                                                    : PyList_GET_ITEM(iterable, i));
        }
        Py_DECREF(iterable);
        ERROR_IF(err < 0);
        DISPATCH();
    }
    kargs[0] = Py_NewRef(list);
    kargs[1] = iterable;
    nk = 2;
    kid = LU_K_list_extend;
    kret = LU_RETURN_DISCARD;
    goto kernel_call;
}

op_DICT_MERGE: {
    PyObject *update = POP();
    SAVE_FRAME();
    if (!PyDict_CheckExact(update) || !lu_dict_str_keys(update)) {
        char buf[256];
        PyObject *name = PyUnicode_FromString(lu_function_str(sp[-oparg - 3], buf, sizeof buf));
        if (name == NULL) {
            Py_DECREF(update);
            goto error;
        }
        kargs[0] = Py_NewRef(sp[-oparg]);
        kargs[1] = update;
        kargs[2] = name;
        nk = 3;
        kid = LU_K_dict_merge;
        kret = LU_RETURN_DISCARD;
        goto kernel_call;
    }
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
    if (!PyDict_CheckExact(update) || !lu_dict_str_keys(update)) {
        kargs[0] = Py_NewRef(dict);
        kargs[1] = update;
        nk = 2;
        kid = LU_K_dict_update;
        kret = LU_RETURN_DISCARD;
        goto kernel_call;
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
    SAVE_FRAME();
    if (oparg == INTRINSIC_STOPITERATION_ERROR) {
        /* PEP 479: StopIteration leaving a generator's body becomes RuntimeError */
        kargs[0] = POP();
        kargs[1] = lu_small_int((code->co_flags & CO_ASYNC_GENERATOR) ? 2
                                : (code->co_flags & CO_COROUTINE) ? 1 : 0);
        nk = 2;
        kid = LU_K_stopiteration_error;
        kret = LU_RETURN_PUSH;
        goto kernel_call;
    }
    if (oparg == INTRINSIC_ASYNC_GEN_WRAP) {
        PyObject *w = lu_async_gen_wrap(TOP());
        ERROR_IF(w == NULL);
        Py_SETREF(TOP(), w);
        DISPATCH();
    }
    if (oparg == INTRINSIC_UNARY_POSITIVE) {
        if (!PyLong_CheckExact(TOP())) {
            kargs[0] = POP();
            nk = 1;
            kid = LU_K_positive;
            kret = LU_RETURN_PUSH;
            goto kernel_call;
        }
        DISPATCH();
    }
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
    if (Py_IS_TYPE(iterable, &PyGen_Type)) {
        DISPATCH();
    }
    SAVE_FRAME();
    PyObject *it = get_iter_exact(iterable);
    if (it == NULL) {
        ERROR_IF(PyErr_Occurred());
        kargs[0] = POP();
        nk = 1;
        kid = LU_K_get_iter;
        kret = LU_RETURN_PUSH;
        goto kernel_call;
    }
    TOP() = it;
    Py_DECREF(iterable);
    DISPATCH();
}

op_FOR_ITER: {
    /* iter -- iter, next; exhausted, jumps past the END_FOR at the target */
    SKIP_CACHES();
    PyObject *iter = TOP();
    PyTypeObject *itp = Py_TYPE(iter);
    PyObject *v;
    if (itp == &PyListIter_Type || itp == &PyTupleIter_Type) {
        v = lu_seqiter_next(iter);
    }
    else if (itp == &PyRangeIter_Type) {
        v = lu_rangeiter_next(iter);
    }
    else if (itp == &PyGen_Type && ts->interp->eval_frame == NULL) {
        SAVE_FRAME();
        callee = lu_gen_resume(ts, iter, NULL);
        if (callee == NULL) {
            ERROR_IF(PyErr_Occurred());
            next_instr += oparg + 1;
            DISPATCH();
        }
        callee->ret = LU_RETURN_FOR_ITER;
        goto resume_linked;
    }
    else {
        SAVE_FRAME();
        kargs[0] = Py_NewRef(iter);
        nk = 1;
        kid = LU_K_next_item;
        kret = LU_RETURN_FOR_ITER;
        goto kernel_call;
    }
    if (v == NULL) {
        SAVE_FRAME();
        ERROR_IF(PyErr_Occurred());
        next_instr += oparg + 1;
        DISPATCH();
    }
    PUSH(v);
    DISPATCH();
}

op_COMPARE_OP: {
    /* Exact str equality and exact ints here; the rest is the kernel's compare. */
    SKIP_CACHES();
    PyObject *right = sp[-1];
    PyObject *left = sp[-2];
    int op = oparg >> 5, r;
    if (PyUnicode_CheckExact(left) && PyUnicode_CheckExact(right) && (op == Py_EQ || op == Py_NE)) {
        r = lu_str_eq(left, right) == (op == Py_EQ);
    }
    else if (PyLong_CheckExact(left) && PyLong_CheckExact(right)) {
        int c = lu_long_compare(left, right);
        r = op == Py_LT ? c < 0 : op == Py_LE ? c <= 0 : op == Py_EQ ? c == 0
          : op == Py_NE ? c != 0 : op == Py_GT ? c > 0 : c >= 0;
    }
    else {
        sp -= 2;
        kargs[0] = left;
        kargs[1] = right;
        kargs[2] = lu_small_int(oparg);
        nk = 3;
        kid = LU_K_compare;
        kret = LU_RETURN_PUSH;
        goto kernel_call;
    }
    sp--;
    TOP() = r ? Py_True : Py_False;
    Py_DECREF(left);
    Py_DECREF(right);
    DISPATCH();
}

op_FORMAT_SIMPLE: {
    /* value -- str(value) through PyObject_Format(value, '') */
    if (PyUnicode_CheckExact(TOP())) {
        DISPATCH();
    }
    kargs[0] = POP();
    kargs[1] = (PyObject *)&_PyRuntime.singletons.empty_str;
    nk = 2;
    kid = LU_K_format;
    kret = LU_RETURN_PUSH;
    goto kernel_call;
}

op_FORMAT_WITH_SPEC: {
    /* value, spec -- PyObject_Format(value, spec) */
    sp -= 2;
    kargs[0] = sp[0];
    kargs[1] = sp[1];
    nk = 2;
    kid = LU_K_format;
    kret = LU_RETURN_PUSH;
    goto kernel_call;
}

op_CONVERT_VALUE: {
    /* value -- str(value), repr(value) or ascii(value) for oparg 1, 2, 3 */
    kargs[0] = POP();
    nk = 1;
    kid = oparg == 1 ? LU_K_str : oparg == 2 ? LU_K_repr : LU_K_ascii;
    kret = LU_RETURN_PUSH;
    goto kernel_call;
}

op_BUILD_STRING: {
    /* pieces... -- the pieces joined; every piece is an exact str */
    PyObject *pieces = PyTuple_New(oparg);
    ERROR_IF(pieces == NULL);
    sp -= oparg;
    for (int i = 0; i < oparg; i++) {
        PyTuple_SET_ITEM(pieces, i, sp[i]);
    }
    PyObject *res = lu_str_join_tuple((PyObject *)&_PyRuntime.singletons.empty_str, pieces);
    Py_DECREF(pieces);
    ERROR_IF(res == NULL);
    PUSH(res);
    DISPATCH();
}

op_BINARY_OP: {
    /* lhs, rhs -- res; exact ints here, everything else reflects in the kernel. */
    SKIP_CACHES();
    PyObject *right = sp[-1];
    PyObject *left = sp[-2];
    if (oparg == 26) {   /* NB_SUBSCR */ 
        PyObject **slot = subscr_slot(left, right);
        sp -= 2;
        if (slot != NULL) {
            PUSH(Py_NewRef(*slot));
            Py_DECREF(left);
            Py_DECREF(right);
            DISPATCH();
        }
        kargs[0] = left;
        kargs[1] = right;
        nk = 2;
        kid = LU_K_getitem;
        kret = LU_RETURN_PUSH;
        goto kernel_call;
    }
    if (PyLong_CheckExact(left) && PyLong_CheckExact(right)) {
        SAVE_FRAME();
        PyObject *res = lu_long_binary(oparg, left, right);
        ERROR_IF(res == NULL);
        if (res != Py_NotImplemented) {
            sp--;
            TOP() = res;
            Py_DECREF(left);
            Py_DECREF(right);
            DISPATCH();
        }
        Py_DECREF(res);
    }
    else if ((oparg == NB_ADD || oparg == NB_INPLACE_ADD) && PyUnicode_CheckExact(left)
             && PyUnicode_CheckExact(right)) {
        SAVE_FRAME();
        PyObject *res = lu_str_concat(left, right);
        ERROR_IF(res == NULL);
        sp--;
        TOP() = res;
        Py_DECREF(left);
        Py_DECREF(right);
        DISPATCH();
    }
    sp -= 2;
    kargs[0] = left;
    kargs[1] = right;
    kargs[2] = lu_small_int(oparg);
    nk = 3;
    kid = LU_K_binary_op;
    kret = LU_RETURN_PUSH;
    goto kernel_call;
}

op_UNARY_NEGATIVE:
op_UNARY_INVERT: {
    PyObject *v = TOP();
    if (PyLong_CheckExact(v)) {
        PyObject *res = opcode == UNARY_NEGATIVE ? lu_long_negate(v) : lu_long_invert(v);
        if (res == NULL) {
            SAVE_FRAME();
            goto error;
        }
        TOP() = res;
        Py_DECREF(v);
        DISPATCH();
    }
    kargs[0] = POP();
    nk = 1;
    kid = opcode == UNARY_NEGATIVE ? LU_K_negative : LU_K_invert;
    kret = LU_RETURN_PUSH;
    goto kernel_call;
}

op_CONTAINS_OP: {
    /* item, container -- bool; oparg 1 is `not in` */
    SKIP_CACHES();
    sp -= 2;
    kargs[0] = sp[1];
    kargs[1] = sp[0];
    kargs[2] = lu_small_int(oparg);
    nk = 3;
    kid = LU_K_contains;
    kret = LU_RETURN_PUSH;
    goto kernel_call;
}

op_UNPACK_SEQUENCE: {
    SKIP_CACHES();
    PyObject *seq = POP();
    if (unpack_exact(seq, oparg, sp)) {
        Py_DECREF(seq);
        sp += oparg;
        DISPATCH();
    }
    SAVE_FRAME();
    kargs[0] = seq;
    kargs[1] = lu_small_int(oparg);
    nk = 2;
    kid = LU_K_unpack;
    kret = LU_RETURN_UNPACK;
    goto kernel_call;
}

op_UNPACK_EX: {
    SAVE_FRAME();
    kargs[0] = POP();
    kargs[1] = lu_small_int(oparg & 0xFF);
    kargs[2] = lu_small_int(oparg >> 8);
    nk = 3;
    kid = LU_K_unpack_ex;
    kret = LU_RETURN_UNPACK;
    goto kernel_call;
}

op_STORE_SUBSCR: {
    /* value, container, sub -- */
    SKIP_CACHES();
    PyObject *sub = sp[-1];
    PyObject *container = sp[-2];
    PyObject **slot = subscr_slot(container, sub);
    if (slot != NULL && PyList_CheckExact(container)) {
        PyObject *value = sp[-3];
        PyObject *old = *slot;
        *slot = value;
        sp -= 3;
        Py_DECREF(old);
        Py_DECREF(container);
        Py_DECREF(sub);
        DISPATCH();
    }
    sp -= 3;
    kargs[0] = container;
    kargs[1] = sub;
    kargs[2] = sp[0];
    nk = 3;
    kid = LU_K_setitem;
    kret = LU_RETURN_DISCARD;
    goto kernel_call;
}

op_DELETE_SUBSCR: {
    /* container, sub -- */
    sp -= 2;
    kargs[0] = sp[0];
    kargs[1] = sp[1];
    nk = 2;
    kid = LU_K_delitem;
    kret = LU_RETURN_DISCARD;
    goto kernel_call;
}

op_BUILD_SLICE: {
    /* start, stop[, step] -- slice */
    PyObject *step = oparg == 3 ? POP() : NULL;
    PyObject *stop = POP();
    PyObject *start = TOP();
    SAVE_FRAME();
    PyObject *slice = PySlice_New(start, stop, step);
    Py_DECREF(start);
    Py_DECREF(stop);
    Py_XDECREF(step);
    ERROR_IF(slice == NULL);
    TOP() = slice;
    DISPATCH();
}

op_BINARY_SLICE: {
    /* container, start, stop -- container[start:stop] */
    SAVE_FRAME();
    PyObject *stop = POP();
    PyObject *start = POP();
    PyObject *slice = PySlice_New(start, stop, NULL);
    Py_DECREF(start);
    Py_DECREF(stop);
    if (slice == NULL) {
        Py_DECREF(POP());
        goto error;
    }
    kargs[0] = POP();
    kargs[1] = slice;
    nk = 2;
    kid = LU_K_getitem;
    kret = LU_RETURN_PUSH;
    goto kernel_call;
}

op_STORE_SLICE: {
    /* value, container, start, stop -- container[start:stop] = value */
    SAVE_FRAME();
    PyObject *stop = POP();
    PyObject *start = POP();
    PyObject *slice = PySlice_New(start, stop, NULL);
    Py_DECREF(start);
    Py_DECREF(stop);
    if (slice == NULL) {
        Py_DECREF(POP());
        Py_DECREF(POP());
        goto error;
    }
    kargs[1] = slice;
    kargs[0] = POP();
    kargs[2] = POP();
    nk = 3;
    kid = LU_K_setitem;
    kret = LU_RETURN_DISCARD;
    goto kernel_call;
}

op_GET_LEN: {
    /* obj -- obj, len(obj) */
    SAVE_FRAME();
    kargs[0] = Py_NewRef(TOP());
    nk = 1;
    kid = LU_K_length;
    kret = LU_RETURN_PUSH;
    goto kernel_call;
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
    if (frame->owner == LU_FRAME_GENERATOR) {
        lu_gen_finish(ts, frame);
    }
    else {
        lu_frame_pop(ts, frame);
    }
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
lu_eval(PyThreadState *ts, struct lu_frame *frame)
{
    return eval_frame(ts, frame, 0);
}

PyObject *
_PyEval_EvalFrameDefault(PyThreadState *ts, struct _PyInterpreterFrame *f, int throwflag)
{
    return eval_frame(ts, (struct lu_frame *)f, throwflag);
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
