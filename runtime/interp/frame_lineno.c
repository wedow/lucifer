/* The f_lineno setter (Objects/frameobject.c of CPython 3.14): a trace
   function jumps within its frame by setting the line. The stack is modelled
   as a 64-bit integer of three-bit kinds, to decide which jumps are safe and
   how many values the jump pops. */

#include "interp/instrument.h"
#include "interp/gen.h"
#include "interp/code.h"
#include "opcode.h"
#include "object/dict.h"

typedef enum kind {
    Iterator = 1,
    Except = 2,
    Object = 3,
    Null = 4,
    Lasti = 5,
} Kind;

static int
compatible_kind(Kind from, Kind to)
{
    if (to == 0) {
        return 0;
    }
    if (to == Object) {
        return from != Null;
    }
    if (to == Null) {
        return 1;
    }
    return from == to;
}

#define BITS_PER_BLOCK 3
#define UNINITIALIZED -2
#define OVERFLOWED -1
#define MAX_STACK_ENTRIES (63 / BITS_PER_BLOCK)
#define WILL_OVERFLOW (1ULL << ((MAX_STACK_ENTRIES - 1) * BITS_PER_BLOCK))
#define EMPTY_STACK 0
#define MASK ((1 << BITS_PER_BLOCK) - 1)

static inline int64_t
push_value(int64_t stack, Kind kind)
{
    if (((uint64_t)stack) >= WILL_OVERFLOW) {
        return OVERFLOWED;
    }
    return (stack << BITS_PER_BLOCK) | kind;
}

static inline int64_t
pop_value(int64_t stack)
{
    return stack >> BITS_PER_BLOCK;
}

static inline Kind
top_of_stack(int64_t stack)
{
    return stack & MASK;
}

static inline Kind
peek(int64_t stack, int n)
{
    return (stack >> (BITS_PER_BLOCK * (n - 1))) & MASK;
}

static int64_t
stack_swap(int64_t stack, int n)
{
    Kind to_swap = peek(stack, n);
    Kind top = top_of_stack(stack);
    int shift = BITS_PER_BLOCK * (n - 1);
    int64_t replaced_low = (stack & ~((int64_t)MASK << shift)) | ((int64_t)top << shift);
    return (replaced_low & ~(int64_t)MASK) | to_swap;
}

static int64_t
pop_to_level(int64_t stack, int level)
{
    if (level == 0) {
        return EMPTY_STACK;
    }
    int64_t max_item = (1 << BITS_PER_BLOCK) - 1;
    int64_t level_max_stack = max_item << ((level - 1) * BITS_PER_BLOCK);
    while (stack > level_max_stack) {
        stack = pop_value(stack);
    }
    return stack;
}

static const uint8_t *
parse_varint(const uint8_t *p, int *result)
{
    int val = p[0] & 63;
    while (p[0] & 64) {
        p++;
        val = (val << 6) | (p[0] & 63);
    }
    *result = val;
    return p + 1;
}

/* The modelled stack before each code unit, or NULL with MemoryError. */
static int64_t *
mark_stacks(PyCodeObject *code, int len)
{
    int64_t *stacks = PyMem_Malloc(sizeof(int64_t) * (len + 1));
    if (stacks == NULL) {
        PyErr_NoMemory();
        return NULL;
    }
    for (int i = 1; i <= len; i++) {
        stacks[i] = UNINITIALIZED;
    }
    stacks[0] = EMPTY_STACK;
    int todo = 1;
    while (todo) {
        todo = 0;
        for (int i = 0; i < len;) {
            int j;
            int64_t next_stack = stacks[i];
            lu_codeunit inst = lu_code_start(code)[i];
            int opcode = lu_base_opcode(code, i);
            int oparg = 0;
            while (opcode == EXTENDED_ARG) {
                oparg = (oparg << 8) | inst.op.arg;
                i++;
                inst = lu_code_start(code)[i];
                opcode = lu_base_opcode(code, i);
                stacks[i] = next_stack;
            }
            oparg = (oparg << 8) | inst.op.arg;
            int next_i = i + lu_opcode_caches[opcode] + 1;
            if (next_stack == UNINITIALIZED) {
                i = next_i;
                continue;
            }
            switch (opcode) {
            case POP_JUMP_IF_FALSE:
            case POP_JUMP_IF_TRUE:
            case POP_JUMP_IF_NONE:
            case POP_JUMP_IF_NOT_NONE: {
                j = next_i + oparg;
                next_stack = pop_value(next_stack);
                stacks[j] = next_stack;
                stacks[next_i] = next_stack;
                break;
            }
            case SEND:
                j = oparg + i + 1 + 1;
                stacks[j] = next_stack;
                stacks[next_i] = next_stack;
                break;
            case JUMP_FORWARD:
                j = oparg + i + 1;
                stacks[j] = next_stack;
                break;
            case JUMP_BACKWARD:
            case JUMP_BACKWARD_NO_INTERRUPT:
                j = next_i - oparg;
                if (stacks[j] == UNINITIALIZED && j < i) {
                    todo = 1;
                }
                stacks[j] = next_stack;
                break;
            case GET_ITER:
            case GET_AITER:
                next_stack = push_value(pop_value(next_stack), Iterator);
                stacks[next_i] = next_stack;
                break;
            case FOR_ITER: {
                int64_t target_stack = push_value(next_stack, Object);
                stacks[next_i] = target_stack;
                j = oparg + 1 + 1 + i;
                stacks[j] = target_stack;
                break;
            }
            case END_ASYNC_FOR:
                next_stack = pop_value(pop_value(next_stack));
                stacks[next_i] = next_stack;
                break;
            case PUSH_EXC_INFO:
                next_stack = push_value(next_stack, Except);
                stacks[next_i] = next_stack;
                break;
            case POP_EXCEPT:
                next_stack = pop_value(next_stack);
                stacks[next_i] = next_stack;
                break;
            case RETURN_VALUE:
            case RAISE_VARARGS:
            case RERAISE:
                break;              /* the end of a block */
            case PUSH_NULL:
                next_stack = push_value(next_stack, Null);
                stacks[next_i] = next_stack;
                break;
            case LOAD_GLOBAL:
                next_stack = push_value(next_stack, Object);
                if (oparg & 1) {
                    next_stack = push_value(next_stack, Null);
                }
                stacks[next_i] = next_stack;
                break;
            case LOAD_ATTR:
                if (oparg & 1) {
                    next_stack = pop_value(next_stack);
                    next_stack = push_value(next_stack, Object);
                    next_stack = push_value(next_stack, Null);
                }
                stacks[next_i] = next_stack;
                break;
            case SWAP:
                next_stack = stack_swap(next_stack, oparg);
                stacks[next_i] = next_stack;
                break;
            case COPY:
                next_stack = push_value(next_stack, peek(next_stack, oparg));
                stacks[next_i] = next_stack;
                break;
            default: {
                int delta = PyCompile_OpcodeStackEffect(opcode, oparg);
                if (delta == PY_INVALID_STACK_EFFECT) {
                    PyMem_Free(stacks);
                    PyErr_Format(PyExc_SystemError, "no stack effect for opcode %d", opcode);
                    return NULL;
                }
                while (delta < 0) {
                    next_stack = pop_value(next_stack);
                    delta++;
                }
                while (delta > 0) {
                    next_stack = push_value(next_stack, Object);
                    delta--;
                }
                stacks[next_i] = next_stack;
            }
            }
            i = next_i;
        }
        /* The exception table: a handler starts with the stack its range had,
           cut to the handler's depth, with the exception on top. */
        const uint8_t *start = (const uint8_t *)PyBytes_AS_STRING(code->co_exceptiontable);
        const uint8_t *end = start + PyBytes_GET_SIZE(code->co_exceptiontable);
        const uint8_t *scan = start;
        while (scan < end) {
            int start_offset, size, handler, depth_and_lasti;
            scan = parse_varint(scan, &start_offset);
            scan = parse_varint(scan, &size);
            scan = parse_varint(scan, &handler);
            scan = parse_varint(scan, &depth_and_lasti);
            int level = depth_and_lasti >> 1;
            int lasti = depth_and_lasti & 1;
            if (stacks[start_offset] != UNINITIALIZED && stacks[handler] == UNINITIALIZED) {
                todo = 1;
                uint64_t target_stack = pop_to_level(stacks[start_offset], level);
                if (lasti) {
                    target_stack = push_value(target_stack, Lasti);
                }
                target_stack = push_value(target_stack, Except);
                stacks[handler] = target_stack;
            }
        }
    }
    return stacks;
}

static int
compatible_stack(int64_t from_stack, int64_t to_stack)
{
    if (from_stack < 0 || to_stack < 0) {
        return 0;
    }
    while (from_stack > to_stack) {
        from_stack = pop_value(from_stack);
    }
    while (from_stack) {
        Kind from_top = top_of_stack(from_stack);
        Kind to_top = top_of_stack(to_stack);
        if (!compatible_kind(from_top, to_top)) {
            return 0;
        }
        from_stack = pop_value(from_stack);
        to_stack = pop_value(to_stack);
    }
    return to_stack == 0;
}

static const char *
explain_incompatible_stack(int64_t to_stack)
{
    if (to_stack == OVERFLOWED) {
        return "stack is too deep to analyze";
    }
    if (to_stack == UNINITIALIZED) {
        return "can't jump into an exception handler, or code may be unreachable";
    }
    switch (top_of_stack(to_stack)) {
    case Except:
        return "can't jump into an 'except' block as there's no exception";
    case Lasti:
        return "can't jump into a re-raising block as there's no location";
    case Iterator:
        return "can't jump into the body of a for loop";
    default:
        return "incompatible stacks";
    }
}

/* The first unit of each line, by line number (-1 elsewhere). */
static int *
marklines(PyCodeObject *code, int len)
{
    int *linestarts = PyMem_Malloc(sizeof(int) * len);
    if (linestarts == NULL) {
        PyErr_NoMemory();
        return NULL;
    }
    for (int i = 0; i < len; i++) {
        linestarts[i] = -1;
    }
    struct lu_addr_range bounds;
    lu_range_init(code, &bounds);
    int last_line = -1;
    while (lu_range_advance(&bounds)) {
        int line = bounds.no_line ? -1 : bounds.line;
        if (line != last_line && line != -1) {
            linestarts[bounds.start / sizeof(lu_codeunit)] = line;
            last_line = line;
        }
    }
    return linestarts;
}

static int
first_line_not_before(int *lines, int len, int line)
{
    int result = INT_MAX;
    for (int i = 0; i < len; i++) {
        if (lines[i] < result && lines[i] >= line) {
            result = lines[i];
        }
    }
    return result == INT_MAX ? -1 : result;
}

static int
frame_is_suspended(struct lu_frame *f)
{
    if (lu_frame_in_gen(f)) {
        int state = lu_gen_of(f)->frame_state;
        return state == LU_GEN_SUSPENDED || state == LU_GEN_SUSPENDED_YIELD_FROM;
    }
    return 0;
}

/* warnings.warn(RuntimeWarning), once the program has imported warnings. */
static int
warn_unbound(int unbound)
{
    PyObject *warnings = lu_ns_lookup(PyInterpreterState_Get()->modules, PyUnicode_InternFromString("warnings"));
    PyObject *warn = warnings != NULL ? lu_ns_lookup(PyModule_GetDict(warnings), PyUnicode_InternFromString("warn")) : NULL;
    if (warn == NULL) {
        return 0;
    }
    char text[80];
    snprintf(text, sizeof text, "assigning None to %d unbound local%s", unbound, unbound == 1 ? "" : "s");
    PyObject *args[3] = { PyUnicode_FromString(text), PyExc_RuntimeWarning, NULL };
    args[2] = lu_small_int(1);
    PyObject *res = args[0] != NULL ? lu_call(warn, args, 3, NULL) : NULL;
    Py_XDECREF(args[0]);
    Py_XDECREF(res);
    return res == NULL ? -1 : 0;
}

/* Setting f_lineno from a trace function jumps to the line, subject to
   restrictions: not into an exception handler or a for loop (the iterator must
   be on the stack), and not from a 'return' or 'exception' event, because the
   loop has been left by then. Returns 0, or -1 with ValueError. */
int
lu_frame_set_lineno(struct lu_frame *f, PyObject *value)
{
    PyCodeObject *code = f->code;
    if (!PyLong_CheckExact(value)) {
        PyErr_SetString(PyExc_ValueError, "lineno must be an integer");
        return -1;
    }
    int is_suspended = frame_is_suspended(f);
    /* The historical restrictions: no jumps on 'return' (but after a yield), none from
       'call', and none when not tracing, as this is a debugging feature. */
    int what_event = PyThreadState_Get()->what_event;
    if (what_event < 0) {
        PyErr_SetString(PyExc_ValueError, "f_lineno can only be set in a trace function");
        return -1;
    }
    switch (what_event) {
    case PY_MONITORING_EVENT_PY_RESUME:
    case PY_MONITORING_EVENT_JUMP:
    case PY_MONITORING_EVENT_BRANCH:
    case PY_MONITORING_EVENT_BRANCH_LEFT:
    case PY_MONITORING_EVENT_BRANCH_RIGHT:
    case PY_MONITORING_EVENT_LINE:
    case PY_MONITORING_EVENT_PY_YIELD:
        break;
    case PY_MONITORING_EVENT_PY_START:
        PyErr_SetString(PyExc_ValueError, "can't jump from the 'call' trace event of a new frame");
        return -1;
    case PY_MONITORING_EVENT_CALL:
    case PY_MONITORING_EVENT_C_RETURN:
        PyErr_SetString(PyExc_ValueError, "can't jump during a call");
        return -1;
    case PY_MONITORING_EVENT_PY_RETURN:
    case PY_MONITORING_EVENT_PY_UNWIND:
    case PY_MONITORING_EVENT_PY_THROW:
    case PY_MONITORING_EVENT_RAISE:
    case PY_MONITORING_EVENT_C_RAISE:
    case PY_MONITORING_EVENT_INSTRUCTION:
    case PY_MONITORING_EVENT_EXCEPTION_HANDLED:
        PyErr_SetString(PyExc_ValueError, "can only jump from a 'line' trace event");
        return -1;
    default:
        PyErr_SetString(PyExc_SystemError, "unexpected event type");
        return -1;
    }
    long l_new_lineno;
    if (lu_long_as_long(value, &l_new_lineno) < 0 || l_new_lineno > INT_MAX || l_new_lineno < INT_MIN) {
        PyErr_Clear();
        PyErr_SetString(PyExc_ValueError, "lineno out of range");
        return -1;
    }
    int new_lineno = (int)l_new_lineno;
    if (new_lineno < code->co_firstlineno) {
        PyErr_Format(PyExc_ValueError, "line %d comes before the current code block", new_lineno);
        return -1;
    }
    int len = (int)Py_SIZE(code);
    int *lines = marklines(code, len);
    if (lines == NULL) {
        return -1;
    }
    new_lineno = first_line_not_before(lines, len, new_lineno);
    if (new_lineno < 0) {
        PyErr_Format(PyExc_ValueError, "line %d comes after the current code block", (int)l_new_lineno);
        PyMem_Free(lines);
        return -1;
    }
    int64_t *stacks = mark_stacks(code, len);
    if (stacks == NULL) {
        PyMem_Free(lines);
        return -1;
    }
    int64_t best_stack = OVERFLOWED;
    int best_addr = -1;
    int64_t start_stack = stacks[lu_frame_lasti(f)];
    int err = -1;
    const char *msg = "cannot find bytecode for specified line";
    for (int i = 0; i < len; i++) {
        if (lines[i] == new_lineno) {
            int64_t target_stack = stacks[i];
            if (compatible_stack(start_stack, target_stack)) {
                err = 0;
                if (target_stack > best_stack) {
                    best_stack = target_stack;
                    best_addr = i;
                }
            }
            else if (err < 0) {
                if (start_stack == OVERFLOWED) {
                    msg = "stack to deep to analyze";
                }
                else if (start_stack == UNINITIALIZED) {
                    msg = "can't jump from unreachable code";
                }
                else {
                    msg = explain_incompatible_stack(target_stack);
                    err = 1;
                }
            }
        }
    }
    PyMem_Free(stacks);
    PyMem_Free(lines);
    if (err) {
        PyErr_SetString(PyExc_ValueError, msg);
        return -1;
    }
    /* Bind None to the locals the compiler may have proven to exist at the new
       place. Counting every unbound one is overly cautious, but a full flow
       analysis is too expensive. */
    int unbound = 0;
    for (int i = 0; i < code->co_nlocalsplus; i++) {
        unbound += f->localsplus[i] == NULL;
    }
    if (unbound) {
        if (warn_unbound(unbound) < 0) {
            return -1;
        }
        for (int i = 0; i < code->co_nlocalsplus; i++) {
            if (f->localsplus[i] == NULL) {
                f->localsplus[i] = Py_NewRef(Py_None);
            }
        }
    }
    if (is_suspended) {
        start_stack = pop_value(start_stack);      /* the value the yield popped */
    }
    PyThreadState *ts = PyThreadState_Get();
    while (start_stack > best_stack) {
        PyObject *popped = *--f->stackpointer;
        if (top_of_stack(start_stack) == Except) {
            /* The exception stack goes with the evaluation stack. */
            PyObject *old = ts->exc_info->exc_value;
            ts->exc_info->exc_value = popped == Py_None ? NULL : popped;
            if (popped == Py_None) {
                Py_DECREF(popped);
            }
            Py_XDECREF(old);
        }
        else {
            Py_XDECREF(popped);
        }
        start_stack = pop_value(start_stack);
    }
    f->instr = lu_code_start(code) + best_addr;
    return 0;
}
