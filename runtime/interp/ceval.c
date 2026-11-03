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
    [FOR_ITER] = 1, [CALL] = 3, [CALL_KW] = 3, [BINARY_OP] = 5,
};

#define NAME_ERROR_MSG "name '%.200s' is not defined"
#define UNBOUNDLOCAL_ERROR_MSG \
    "cannot access local variable '%s' where it is not associated with a value"

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
        lu_err_format(PyExc_UnboundLocalError, UNBOUNDLOCAL_ERROR_MSG,
                      PyUnicode_AsUTF8(PyTuple_GET_ITEM(code->co_localsplusnames, oparg)));
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

op_CALL: {
    /* callable, self_or_null, args[oparg] */
    SKIP_CACHES();
    PyObject **base = sp - oparg - 2;
    PyObject **args = sp - oparg;
    PyObject *callable = args[-2];
    Py_ssize_t total = oparg;
    if (args[-1] != NULL) {
        args--;
        total++;
    }
    if (Py_IS_TYPE(callable, &PyFunction_Type)) {
        SAVE_FRAME();
        struct lu_frame *callee = lu_frame_push(ts, (PyFunctionObject *)callable, NULL);
        ERROR_IF(callee == NULL);
        /* The callee owns the arguments now. */
        int err = lu_frame_bind(ts, callee, args, total);
        Py_DECREF(callable);
        sp = base;
        frame->stackpointer = sp;
        if (err < 0) {
            lu_frame_pop(ts, callee);
            goto error;
        }
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
    SAVE_FRAME();
    if (vector == NULL) {
        lu_err_format(PyExc_TypeError, "'%s' object is not callable", Py_TYPE(callable)->tp_name);
        result = NULL;
    }
    else {
        /* Foreign C pins the task; kernel primitives will be exempted when
           migration is on (DESIGN 5.4). */
        struct lu_task *t = (struct lu_task *)ts;
        t->pin++;
        result = vector(callable, args, total, NULL);
        t->pin--;
    }
    for (Py_ssize_t i = 0; i < total; i++) {
        Py_DECREF(args[i]);
    }
    Py_DECREF(callable);
    sp = base;
    ERROR_IF(result == NULL);
    PUSH(result);
    POLL();
    DISPATCH();
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
