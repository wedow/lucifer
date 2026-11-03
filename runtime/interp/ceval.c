/* The eval loop (DESIGN 6.1): computed-goto dispatch over CPython 3.14
   bytecode, semantics from Python/bytecodes.c written by hand. One
   activation runs all of a task's Python frames: a Python call pushes a frame
   and dispatches into it, a return pops it, and only C callers start a new
   activation (lu_eval). The loop polls the carrier's word at calls and
   back-edges, and RESUME compares the code's monitoring version.

   Stack entries are strong references. Exception tables arrive with K1k: an
   error unwinds every frame of the activation. */

#include "interp/frame.h"

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

#define INTRINSIC_LIST_TO_TUPLE 6   /* pycore_intrinsics.h */

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

/* PyObject_IsTrue over the types that exist so far: the singletons, str, and
   objects that define no truth. */
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
    PyObject *result;
    /* The call tail's operands: the callable and the owned argument values,
       positional first, then the keywords named by kwnames. */
    PyObject *callable, *kwnames, **args, **base, **to_free;
    Py_ssize_t total, npos;
    struct lu_frame *callee;

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
#define PUSH(v) (*sp++ = (v))
#define POP() (*--sp)
#define TOP() (sp[-1])
#define GETLOCAL(i) (localsplus[i])
#define NAME(i) PyTuple_GET_ITEM(code->co_names, i)
#define ERROR_IF(cond) do { if (cond) goto error; } while (0)
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
    to_free = NULL;

call_tail:
    /* Consumes callable, args and kwnames; the stack is reset to base. */
    SAVE_FRAME();
    if (Py_IS_TYPE(callable, &PyFunction_Type)) {
        callee = lu_frame_call(ts, (PyFunctionObject *)callable, args, npos, kwnames);
        Py_XDECREF(kwnames);
        Py_DECREF(callable);
        PyMem_Free(to_free);
        sp = base;
        frame->stackpointer = sp;
        ERROR_IF(callee == NULL);
        if (!lu_enter_frame(ts, callee, &result)) {
            ERROR_IF(result == NULL);
            PUSH(result);
            POLL();
            DISPATCH();
        }
        callee->previous = frame;
        ts->current_frame = (struct _PyInterpreterFrame *)callee;
        frame = callee;
        LOAD_FRAME();
        DISPATCH();
    }
    vectorcallfunc vector = PyVectorcall_Function(callable);
    if (vector == NULL) {
        lu_err_format(PyExc_TypeError, "'%s' object is not callable", Py_TYPE(callable)->tp_name);
        result = NULL;
    }
    else {
        /* Foreign C pins the task; kernel primitives will be exempted when
           migration is on (DESIGN 5.4). */
        struct lu_task *t = (struct lu_task *)ts;
        t->pin++;
        result = vector(callable, args, npos, kwnames);
        t->pin--;
    }
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
    to_free = args;
    goto call_tail;
}

op_RETURN_VALUE: {
    PyObject *retval = POP();
    frame->stackpointer = sp;
    struct lu_frame *caller = frame->previous;
    lu_frame_pop(ts, frame);
    if (caller->owner == LU_FRAME_C_ENTRY) {
        ts->current_frame = (struct _PyInterpreterFrame *)caller->previous;
        return retval;
    }
    ts->current_frame = (struct _PyInterpreterFrame *)caller;
    frame = caller;
    LOAD_FRAME();
    PUSH(retval);
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

error:
    /* Until exception tables (K1k): unwind the activation. */
    for (;;) {
        frame->stackpointer = sp;
        struct lu_frame *caller = frame->previous;
        lu_frame_pop(ts, frame);
        if (caller->owner == LU_FRAME_C_ENTRY) {
            ts->current_frame = (struct _PyInterpreterFrame *)caller->previous;
            return NULL;
        }
        frame = caller;
        sp = frame->stackpointer;
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
