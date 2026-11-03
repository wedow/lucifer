/* PEP 669 instrumentation (Python/instrumentation.c of CPython 3.14, without
   the tier-2 executors and thread-local bytecode).

   A code object is instrumented to the interpreter's monitoring version when
   a frame enters it (RESUME); a change of that version is made with the world
   stopped, and the same stop re-instruments every code object a task is
   running, so a hot loop that never reaches RESUME again sees the change at
   its next instruction (FOUNDATIONS 2.3). Instrumenting rewrites opcodes in
   place with atomic byte stores and never moves an instruction, so a task
   running the code keeps its place. Instrumenting at RESUME is serialised per
   code by its critical section and against the specializer by its lock; the
   arrays the monitoring data points to are allocated once and kept, so their
   readers need no reclamation.

   Kernel code and builtin-module ports run as C does in CPython: they are
   never instrumented and fire no events. */

#include "interp/instrument.h"
#include "interp/code.h"
#include "interp/specialize.h"
#include "object/dict.h"

#define MONITORING_VERSION_INCREMENT LU_POLL_VERSION_UNIT

PyObject lu_instrumentation_disable = {
    .ob_ref_local = _Py_IMMORTAL_REFCNT_LOCAL, .ob_flags = _Py_STATICALLY_ALLOCATED_FLAG,
    .ob_type = &PyBaseObject_Type };
PyObject lu_instrumentation_missing = {
    .ob_ref_local = _Py_IMMORTAL_REFCNT_LOCAL, .ob_flags = _Py_STATICALLY_ALLOCATED_FLAG,
    .ob_type = &PyBaseObject_Type };

static const int8_t EVENT_FOR_OPCODE[256] = {
    [RETURN_VALUE] = PY_MONITORING_EVENT_PY_RETURN,
    [INSTRUMENTED_RETURN_VALUE] = PY_MONITORING_EVENT_PY_RETURN,
    [CALL] = PY_MONITORING_EVENT_CALL,
    [INSTRUMENTED_CALL] = PY_MONITORING_EVENT_CALL,
    [CALL_KW] = PY_MONITORING_EVENT_CALL,
    [INSTRUMENTED_CALL_KW] = PY_MONITORING_EVENT_CALL,
    [CALL_FUNCTION_EX] = PY_MONITORING_EVENT_CALL,
    [INSTRUMENTED_CALL_FUNCTION_EX] = PY_MONITORING_EVENT_CALL,
    [LOAD_SUPER_ATTR] = PY_MONITORING_EVENT_CALL,
    [INSTRUMENTED_LOAD_SUPER_ATTR] = PY_MONITORING_EVENT_CALL,
    [RESUME] = -1,
    [YIELD_VALUE] = PY_MONITORING_EVENT_PY_YIELD,
    [INSTRUMENTED_YIELD_VALUE] = PY_MONITORING_EVENT_PY_YIELD,
    [JUMP_FORWARD] = PY_MONITORING_EVENT_JUMP,
    [JUMP_BACKWARD] = PY_MONITORING_EVENT_JUMP,
    [POP_JUMP_IF_FALSE] = PY_MONITORING_EVENT_BRANCH_RIGHT,
    [POP_JUMP_IF_TRUE] = PY_MONITORING_EVENT_BRANCH_RIGHT,
    [POP_JUMP_IF_NONE] = PY_MONITORING_EVENT_BRANCH_RIGHT,
    [POP_JUMP_IF_NOT_NONE] = PY_MONITORING_EVENT_BRANCH_RIGHT,
    [INSTRUMENTED_JUMP_FORWARD] = PY_MONITORING_EVENT_JUMP,
    [INSTRUMENTED_JUMP_BACKWARD] = PY_MONITORING_EVENT_JUMP,
    [INSTRUMENTED_POP_JUMP_IF_FALSE] = PY_MONITORING_EVENT_BRANCH_RIGHT,
    [INSTRUMENTED_POP_JUMP_IF_TRUE] = PY_MONITORING_EVENT_BRANCH_RIGHT,
    [INSTRUMENTED_POP_JUMP_IF_NONE] = PY_MONITORING_EVENT_BRANCH_RIGHT,
    [INSTRUMENTED_POP_JUMP_IF_NOT_NONE] = PY_MONITORING_EVENT_BRANCH_RIGHT,
    [FOR_ITER] = PY_MONITORING_EVENT_BRANCH_LEFT,
    [INSTRUMENTED_FOR_ITER] = PY_MONITORING_EVENT_BRANCH_LEFT,
    [POP_ITER] = PY_MONITORING_EVENT_BRANCH_RIGHT,
    [INSTRUMENTED_POP_ITER] = PY_MONITORING_EVENT_BRANCH_RIGHT,
    [END_FOR] = PY_MONITORING_EVENT_STOP_ITERATION,
    [INSTRUMENTED_END_FOR] = PY_MONITORING_EVENT_STOP_ITERATION,
    [END_SEND] = PY_MONITORING_EVENT_STOP_ITERATION,
    [INSTRUMENTED_END_SEND] = PY_MONITORING_EVENT_STOP_ITERATION,
    [NOT_TAKEN] = PY_MONITORING_EVENT_BRANCH_LEFT,
    [INSTRUMENTED_NOT_TAKEN] = PY_MONITORING_EVENT_BRANCH_LEFT,
    [END_ASYNC_FOR] = PY_MONITORING_EVENT_BRANCH_RIGHT,
};

/* Instrumented opcode -> the opcode it replaces, and back. */
#define INSTRUMENTED_PAIRS(X) \
    X(RESUME) X(RETURN_VALUE) X(CALL) X(CALL_KW) X(CALL_FUNCTION_EX) X(YIELD_VALUE) \
    X(JUMP_FORWARD) X(JUMP_BACKWARD) X(POP_JUMP_IF_FALSE) X(POP_JUMP_IF_TRUE) \
    X(POP_JUMP_IF_NONE) X(POP_JUMP_IF_NOT_NONE) X(FOR_ITER) X(POP_ITER) X(END_FOR) \
    X(END_SEND) X(LOAD_SUPER_ATTR) X(NOT_TAKEN) X(END_ASYNC_FOR)

static uint8_t DE_INSTRUMENT[256], INSTRUMENTED_OPCODES[256];

/* The instrumented form of each opcode, and the cache words of every opcode a
   scan may meet. The tables are filled before the first code object runs. */
void
lu_instrument_init(void)
{
#define PAIR(op) \
    DE_INSTRUMENT[INSTRUMENTED_##op] = op; \
    INSTRUMENTED_OPCODES[op] = INSTRUMENTED_OPCODES[INSTRUMENTED_##op] = INSTRUMENTED_##op;
    INSTRUMENTED_PAIRS(PAIR)
#undef PAIR
    INSTRUMENTED_OPCODES[INSTRUMENTED_LINE] = INSTRUMENTED_LINE;
    INSTRUMENTED_OPCODES[INSTRUMENTED_INSTRUCTION] = INSTRUMENTED_INSTRUCTION;
}

static inline int
opcode_has_event(int opcode)
{
    return opcode != INSTRUMENTED_LINE && INSTRUMENTED_OPCODES[opcode] > 0;
}

static inline int
is_instrumented(int opcode)
{
    return opcode != ENTER_EXECUTOR && opcode >= MIN_INSTRUMENTED_OPCODE;
}

int
lu_opcode_deopt(int opcode)
{
    switch (opcode) {
#define DEOPT(special, generic) case special: return generic;
    LU_SPECIALIZED_OPCODES(DEOPT)
#undef DEOPT
    }
    return opcode;
}

/* Local-monitor set algebra. */

static inline lu_local_monitors
monitors_sub(lu_local_monitors a, lu_local_monitors b)
{
    lu_local_monitors res;
    for (int i = 0; i < LU_MON_LOCAL_EVENTS; i++) {
        res.tools[i] = a.tools[i] & ~b.tools[i];
    }
    return res;
}

/* The local events of the global monitors and a code's own. */
static inline lu_local_monitors
local_union(lu_global_monitors a, lu_local_monitors b)
{
    lu_local_monitors res;
    for (int i = 0; i < LU_MON_LOCAL_EVENTS; i++) {
        res.tools[i] = a.tools[i] | b.tools[i];
    }
    return res;
}

static inline int
monitors_are_empty(lu_local_monitors m)
{
    for (int i = 0; i < LU_MON_LOCAL_EVENTS; i++) {
        if (m.tools[i]) {
            return 0;
        }
    }
    return 1;
}

static inline int
multiple_tools(lu_local_monitors *m)
{
    for (int i = 0; i < LU_MON_LOCAL_EVENTS; i++) {
        if (__builtin_popcount(m->tools[i]) > 1) {
            return 1;
        }
    }
    return 0;
}

static inline lu_event_set
get_local_events(lu_local_monitors *m, int tool_id)
{
    lu_event_set result = 0;
    for (int e = 0; e < LU_MON_LOCAL_EVENTS; e++) {
        if ((m->tools[e] >> tool_id) & 1) {
            result |= (1 << e);
        }
    }
    return result;
}

static inline lu_event_set
get_events(lu_global_monitors *m, int tool_id)
{
    lu_event_set result = 0;
    for (int e = 0; e < LU_MON_UNGROUPED_EVENTS; e++) {
        if ((m->tools[e] >> tool_id) & 1) {
            result |= (1 << e);
        }
    }
    return result;
}

/* Lines: a unit's line is stored as its delta from co_firstlineno, plus two
   so that "no line" fits an unsigned byte string. Module code can have line
   0 while it starts at line 1, so -1 is a legal delta. */
#define NO_LINE (-2)

static int
compute_line_delta(PyCodeObject *code, int line)
{
    return line < 0 ? NO_LINE : line - code->co_firstlineno;
}

static int
compute_line(PyCodeObject *code, int line_delta)
{
    return line_delta == NO_LINE ? -1 : code->co_firstlineno + line_delta;
}

static inline uint8_t
get_original_opcode(lu_line_data *line_data, int index)
{
    return line_data->data[index * line_data->bytes_per_entry];
}

static inline uint8_t *
get_original_opcode_ptr(lu_line_data *line_data, int index)
{
    return &line_data->data[index * line_data->bytes_per_entry];
}

static inline void
set_original_opcode(lu_line_data *line_data, int index, uint8_t opcode)
{
    line_data->data[index * line_data->bytes_per_entry] = opcode;
}

static inline int
get_line_delta(lu_line_data *line_data, int index)
{
    uint8_t *ptr = &line_data->data[index * line_data->bytes_per_entry + 1];
    uint32_t value = *ptr;
    for (int idx = 2; idx < line_data->bytes_per_entry; idx++) {
        ptr++;
        value |= ((uint32_t)(*ptr)) << ((idx - 1) * 8);
    }
    return ((int)value) + NO_LINE;
}

static inline void
set_line_delta(lu_line_data *line_data, int index, int line_delta)
{
    uint32_t adjusted = line_delta - NO_LINE;
    uint8_t *ptr = &line_data->data[index * line_data->bytes_per_entry + 1];
    *ptr = adjusted & 0xff;
    for (int idx = 2; idx < line_data->bytes_per_entry; idx++) {
        ptr++;
        adjusted >>= 8;
        *ptr = adjusted & 0xff;
    }
}

/* The location table, walked once in address order (PyCodeAddressRange). */
struct addr_range {
    const uint8_t *p, *limit;
    int start, end, line, no_line;
};

static int
read_svarint_line(const uint8_t **p)
{
    unsigned int read = *(*p)++;
    unsigned int val = read & 63, shift = 0;
    while (read & 64) {
        read = *(*p)++;
        shift += 6;
        val |= (read & 63) << shift;
    }
    return val & 1 ? -(int)(val >> 1) : (int)(val >> 1);
}

static void
range_init(PyCodeObject *co, struct addr_range *r)
{
    r->p = (const uint8_t *)PyBytes_AS_STRING(co->co_linetable);
    r->limit = r->p + PyBytes_GET_SIZE(co->co_linetable);
    r->start = r->end = 0;
    r->line = co->co_firstlineno;
}

/* Moves to the next entry; 0 at the end of the table. */
static int
range_advance(struct addr_range *r)
{
    if (r->p >= r->limit) {
        return 0;
    }
    int code = (*r->p >> 3) & 15, delta = 0;
    const uint8_t *q = r->p + 1;
    if (code == PY_CODE_LOCATION_INFO_NO_COLUMNS || code == PY_CODE_LOCATION_INFO_LONG) {
        delta = read_svarint_line(&q);
    }
    else if (code >= PY_CODE_LOCATION_INFO_ONE_LINE0 && code <= PY_CODE_LOCATION_INFO_ONE_LINE2) {
        delta = code - PY_CODE_LOCATION_INFO_ONE_LINE0;
    }
    r->line += delta;
    r->start = r->end;
    r->end = r->start + ((*r->p & 7) + 1) * (int)sizeof(lu_codeunit);
    r->no_line = code == 0x1f;
    do {
        r->p++;
    } while (r->p < r->limit && (*r->p & 128) == 0);
    return 1;
}

/* The line of the instruction at byte offset `addr`, ascending calls only
   (_PyCode_CheckLineNumber). */
static int
range_line(int addr, struct addr_range *r)
{
    while (addr >= r->end) {
        if (!range_advance(r)) {
            return -1;
        }
    }
    return r->no_line ? -1 : r->line;
}

/* The exception table's varints are big-endian (parse_varint). */
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

/* Reads unit `i` as its start, and the opcode it stands for: INSTRUMENTED_LINE
   and INSTRUMENTED_INSTRUCTION keep theirs in the monitoring data. */
static int
original_opcode_at(PyCodeObject *code, Py_ssize_t i)
{
    int opcode = __atomic_load_n(&lu_code_start(code)[i].op.code, __ATOMIC_RELAXED);
    lu_monitoring *data = __atomic_load_n(&code->_co_monitoring, __ATOMIC_ACQUIRE);
    /* A cache word can look like either: it has no data to read. */
    if (opcode == INSTRUMENTED_LINE && data != NULL && data->lines != NULL) {
        opcode = get_original_opcode(data->lines, (int)i);
    }
    if (opcode == INSTRUMENTED_INSTRUCTION && data != NULL && data->per_instruction_opcodes != NULL) {
        opcode = data->per_instruction_opcodes[i];
    }
    return opcode;
}

/* _Py_GetBaseCodeUnit's opcode. */
int
lu_base_opcode(PyCodeObject *code, Py_ssize_t i)
{
    int opcode = original_opcode_at(code, i);
    int deinstrumented = DE_INSTRUMENT[opcode];
    return deinstrumented ? deinstrumented : lu_opcode_deopt(opcode);
}

int
lu_line_original_opcode(PyCodeObject *code, lu_codeunit *at)
{
    return get_original_opcode(code->_co_monitoring->lines, (int)(at - lu_code_start(code)));
}

int
lu_instrumented_opcode_at(PyCodeObject *code, lu_codeunit *at)
{
    return original_opcode_at(code, at - lu_code_start(code));
}

int
lu_instrumented_caches(PyCodeObject *code, lu_codeunit *at)
{
    return lu_opcode_caches[lu_base_opcode(code, at - lu_code_start(code))];
}

static int
instruction_length(PyCodeObject *code, int offset)
{
    return 1 + lu_opcode_caches[lu_base_opcode(code, offset)];
}

static void
reset_counter(lu_codeunit *instr, int opcode)
{
    if (lu_opcode_caches[opcode]) {
        lu_cache_store(instr + 1, (1 << 4) | 1);        /* adaptive_counter_warmup */
    }
}

static void
de_instrument(PyCodeObject *code, lu_monitoring *monitoring, int i)
{
    lu_codeunit *instr = &lu_code_start(code)[i];
    uint8_t *opcode_ptr = &instr->op.code;
    int opcode = *opcode_ptr;
    if (opcode == INSTRUMENTED_LINE) {
        opcode_ptr = get_original_opcode_ptr(monitoring->lines, i);
        opcode = *opcode_ptr;
    }
    if (opcode == INSTRUMENTED_INSTRUCTION) {
        opcode_ptr = &monitoring->per_instruction_opcodes[i];
        opcode = *opcode_ptr;
    }
    int deinstrumented = DE_INSTRUMENT[opcode];
    if (deinstrumented == 0) {
        return;
    }
    __atomic_store_n(opcode_ptr, (uint8_t)deinstrumented, __ATOMIC_RELAXED);
    reset_counter(instr, deinstrumented);
}

static void
de_instrument_line(PyCodeObject *code, lu_monitoring *monitoring, int i)
{
    lu_codeunit *instr = &lu_code_start(code)[i];
    if (instr->op.code != INSTRUMENTED_LINE) {
        return;
    }
    int original_opcode = get_original_opcode(monitoring->lines, i);
    __atomic_store_n(&instr->op.code, (uint8_t)original_opcode, __ATOMIC_RELEASE);
    reset_counter(instr, original_opcode);
}

static void
de_instrument_per_instruction(PyCodeObject *code, lu_monitoring *monitoring, int i)
{
    lu_codeunit *instr = &lu_code_start(code)[i];
    uint8_t *opcode_ptr = &instr->op.code;
    int opcode = *opcode_ptr;
    if (opcode == INSTRUMENTED_LINE) {
        opcode_ptr = get_original_opcode_ptr(monitoring->lines, i);
        opcode = *opcode_ptr;
    }
    if (opcode != INSTRUMENTED_INSTRUCTION) {
        return;
    }
    int original_opcode = monitoring->per_instruction_opcodes[i];
    __atomic_store_n(opcode_ptr, (uint8_t)original_opcode, __ATOMIC_RELAXED);
    reset_counter(instr, original_opcode);
}

static void
instrument(PyCodeObject *code, lu_monitoring *monitoring, int i)
{
    lu_codeunit *instr = &lu_code_start(code)[i];
    uint8_t *opcode_ptr = &instr->op.code;
    int opcode = *opcode_ptr;
    if (opcode == INSTRUMENTED_LINE) {
        opcode_ptr = get_original_opcode_ptr(monitoring->lines, i);
        opcode = *opcode_ptr;
    }
    if (opcode == INSTRUMENTED_INSTRUCTION) {
        opcode_ptr = &monitoring->per_instruction_opcodes[i];
        opcode = *opcode_ptr;
    }
    if (!is_instrumented(opcode)) {
        int deopt = lu_opcode_deopt(opcode);
        __atomic_store_n(opcode_ptr, INSTRUMENTED_OPCODES[deopt], __ATOMIC_RELAXED);
        reset_counter(instr, deopt);
    }
}

static void
instrument_line(PyCodeObject *code, lu_monitoring *monitoring, int i)
{
    uint8_t *opcode_ptr = &lu_code_start(code)[i].op.code;
    int opcode = *opcode_ptr;
    if (opcode == INSTRUMENTED_LINE) {
        return;
    }
    set_original_opcode(monitoring->lines, i, lu_opcode_deopt(opcode));
    __atomic_store_n(opcode_ptr, INSTRUMENTED_LINE, __ATOMIC_RELEASE);
}

static void
instrument_per_instruction(PyCodeObject *code, lu_monitoring *monitoring, int i)
{
    lu_codeunit *instr = &lu_code_start(code)[i];
    uint8_t *opcode_ptr = &instr->op.code;
    int opcode = *opcode_ptr;
    if (opcode == INSTRUMENTED_LINE) {
        opcode_ptr = get_original_opcode_ptr(monitoring->lines, i);
        opcode = *opcode_ptr;
    }
    if (opcode == INSTRUMENTED_INSTRUCTION) {
        return;
    }
    monitoring->per_instruction_opcodes[i] = is_instrumented(opcode) ? opcode : lu_opcode_deopt(opcode);
    __atomic_store_n(opcode_ptr, INSTRUMENTED_INSTRUCTION, __ATOMIC_RELAXED);
}

static void
remove_tools(PyCodeObject *code, int offset, int event, int tools)
{
    lu_monitoring *monitoring = code->_co_monitoring;
    int should_de_instrument;
    if (monitoring->tools) {
        monitoring->tools[offset] &= ~tools;
        should_de_instrument = (monitoring->tools[offset] == 0);
    }
    else {
        uint8_t single_tool = monitoring->active_monitors.tools[event];
        should_de_instrument = ((single_tool & tools) == single_tool);
    }
    if (should_de_instrument) {
        de_instrument(code, monitoring, offset);
    }
}

static void
remove_line_tools(PyCodeObject *code, int offset, int tools)
{
    lu_monitoring *monitoring = code->_co_monitoring;
    int should_de_instrument;
    if (monitoring->line_tools) {
        uint8_t *toolsptr = &monitoring->line_tools[offset];
        *toolsptr &= ~tools;
        should_de_instrument = (*toolsptr == 0);
    }
    else {
        uint8_t single_tool = monitoring->active_monitors.tools[PY_MONITORING_EVENT_LINE];
        should_de_instrument = ((single_tool & tools) == single_tool);
    }
    if (should_de_instrument) {
        de_instrument_line(code, monitoring, offset);
        if (get_original_opcode(monitoring->lines, offset) == INSTRUMENTED_INSTRUCTION) {
            set_original_opcode(monitoring->lines, offset, monitoring->per_instruction_opcodes[offset]);
        }
    }
}

static void
add_tools(PyCodeObject *code, int offset, int event, int tools)
{
    if (code->_co_monitoring->tools) {
        code->_co_monitoring->tools[offset] |= tools;
    }
    instrument(code, code->_co_monitoring, offset);
}

static void
add_line_tools(PyCodeObject *code, int offset, int tools)
{
    if (code->_co_monitoring->line_tools) {
        code->_co_monitoring->line_tools[offset] |= tools;
    }
    instrument_line(code, code->_co_monitoring, offset);
}

static void
add_per_instruction_tools(PyCodeObject *code, int offset, int tools)
{
    if (code->_co_monitoring->per_instruction_tools) {
        code->_co_monitoring->per_instruction_tools[offset] |= tools;
    }
    instrument_per_instruction(code, code->_co_monitoring, offset);
}

static void
remove_per_instruction_tools(PyCodeObject *code, int offset, int tools)
{
    lu_monitoring *monitoring = code->_co_monitoring;
    int should_de_instrument;
    if (monitoring->per_instruction_tools) {
        uint8_t *toolsptr = &monitoring->per_instruction_tools[offset];
        *toolsptr &= ~tools;
        should_de_instrument = (*toolsptr == 0);
    }
    else {
        uint8_t single_tool = monitoring->active_monitors.tools[PY_MONITORING_EVENT_INSTRUCTION];
        should_de_instrument = ((single_tool & tools) == single_tool);
    }
    if (should_de_instrument) {
        de_instrument_per_instruction(code, monitoring, offset);
    }
}

/* Calling the tools */

/* 1 if the callback returned DISABLE, -1 on error, 0 otherwise. The callable
   is held across the call: a registration made meanwhile, with the world
   stopped, drops its own reference only after the call has one. */
static int
call_one_instrument(PyInterpreterState *interp, PyThreadState *ts, PyObject **args,
                    size_t nargsf, int8_t tool, int event)
{
    PyObject *instrument = __atomic_load_n(&interp->monitoring_callables[tool][event], __ATOMIC_ACQUIRE);
    if (instrument == NULL) {
        return 0;
    }
    Py_INCREF(instrument);
    int old_what = ts->what_event;
    ts->what_event = event;
    ts->tracing++;
    PyObject *res = PyObject_Vectorcall(instrument, args, nargsf, NULL);
    ts->tracing--;
    ts->what_event = old_what;
    Py_DECREF(instrument);
    if (res == NULL) {
        return -1;
    }
    if (res == &lu_instrumentation_disable) {
        return 1;
    }
    Py_DECREF(res);
    return 0;
}

static int
most_significant_bit(uint8_t bits)
{
    return 31 - __builtin_clz((unsigned)bits);
}

static uint32_t
global_version(PyInterpreterState *interp)
{
    return (uint32_t)__atomic_load_n(&interp->monitoring_version, __ATOMIC_RELAXED);
}

/* Publishes a new version: the interpreter's, which tasks bound from now on
   read, and every bound carrier's poll word, without touching its reasons.
   The world is stopped. */
static void
set_global_version(PyInterpreterState *interp, uint32_t version)
{
    __atomic_store_n(&interp->monitoring_version, version, __ATOMIC_SEQ_CST);
    for (PyThreadState *ts = interp->tasks; ts != NULL; ts = ts->next) {
        struct lu_carrier *c = __atomic_load_n(&((struct lu_task *)ts)->carrier, __ATOMIC_ACQUIRE);
        if (c == NULL) {
            continue;
        }
        uintptr_t old = __atomic_load_n(&c->poll, __ATOMIC_RELAXED), new;
        do {
            new = (old & LU_POLL_EVENTS_MASK) | version;
        } while (!__atomic_compare_exchange_n(&c->poll, &old, new, 0, __ATOMIC_SEQ_CST,
                                              __ATOMIC_RELAXED));
        ts->eval_breaker = new;
    }
}

static int
is_version_up_to_date(PyCodeObject *code, PyInterpreterState *interp)
{
    return global_version(interp) == __atomic_load_n(&code->_co_instrumentation_version, __ATOMIC_ACQUIRE);
}

static inline uint8_t
get_tools_for_instruction(PyCodeObject *code, PyInterpreterState *interp, int i, int event)
{
    if (event >= LU_MON_UNGROUPED_EVENTS) {
        event = PY_MONITORING_EVENT_CALL;       /* C_RAISE and C_RETURN */
    }
    if (PY_MONITORING_IS_INSTRUMENTED_EVENT(event)) {
        lu_monitoring *data = code->_co_monitoring;
        return data->tools != NULL ? data->tools[i] : data->active_monitors.tools[event];
    }
    return interp->monitors.tools[event];
}

static const char *const event_names[] = {
    [PY_MONITORING_EVENT_PY_START] = "PY_START",
    [PY_MONITORING_EVENT_PY_RESUME] = "PY_RESUME",
    [PY_MONITORING_EVENT_PY_RETURN] = "PY_RETURN",
    [PY_MONITORING_EVENT_PY_YIELD] = "PY_YIELD",
    [PY_MONITORING_EVENT_CALL] = "CALL",
    [PY_MONITORING_EVENT_LINE] = "LINE",
    [PY_MONITORING_EVENT_INSTRUCTION] = "INSTRUCTION",
    [PY_MONITORING_EVENT_JUMP] = "JUMP",
    [PY_MONITORING_EVENT_BRANCH] = "BRANCH",
    [PY_MONITORING_EVENT_BRANCH_LEFT] = "BRANCH_LEFT",
    [PY_MONITORING_EVENT_BRANCH_RIGHT] = "BRANCH_RIGHT",
    [PY_MONITORING_EVENT_C_RETURN] = "C_RETURN",
    [PY_MONITORING_EVENT_PY_THROW] = "PY_THROW",
    [PY_MONITORING_EVENT_RAISE] = "RAISE",
    [PY_MONITORING_EVENT_RERAISE] = "RERAISE",
    [PY_MONITORING_EVENT_EXCEPTION_HANDLED] = "EXCEPTION_HANDLED",
    [PY_MONITORING_EVENT_C_RAISE] = "C_RAISE",
    [PY_MONITORING_EVENT_PY_UNWIND] = "PY_UNWIND",
    [PY_MONITORING_EVENT_STOP_ITERATION] = "STOP_ITERATION",
};

const char *
lu_monitoring_event_name(int event)
{
    return event_names[event];
}

static int
call_instrumentation_vector(lu_codeunit *instr, PyThreadState *ts, int event, struct lu_frame *frame,
                            lu_codeunit *arg2, Py_ssize_t nargs, PyObject *args[])
{
    if (ts->tracing) {
        return 0;
    }
    PyCodeObject *code = frame->code;
    args[1] = (PyObject *)code;
    int offset = (int)(instr - lu_code_start(code));
    /* The offset a user sees is in bytes. */
    PyObject *arg2_obj = PyLong_FromLong((long)(arg2 - lu_code_start(code)) * (long)sizeof(lu_codeunit));
    if (arg2_obj == NULL) {
        return -1;
    }
    args[2] = arg2_obj;
    PyInterpreterState *interp = ts->interp;
    uint8_t tools = get_tools_for_instruction(code, interp, offset, event);
    size_t nargsf = (size_t)nargs | PY_VECTORCALL_ARGUMENTS_OFFSET;
    PyObject **callargs = &args[1];
    int err = 0;
    while (tools) {
        int tool = most_significant_bit(tools);
        tools ^= (1 << tool);
        int res = call_one_instrument(interp, ts, callargs, nargsf, tool, event);
        if (res < 0) {
            err = -1;
            break;
        }
        if (res > 0) {
            if (!PY_MONITORING_IS_INSTRUMENTED_EVENT(event)) {
                PyErr_Format(PyExc_ValueError, "Cannot disable %s events. Callback removed.",
                             event_names[event]);
                /* Clear the tool to prevent an infinite loop. */
                Py_CLEAR(interp->monitoring_callables[tool][event]);
                err = -1;
                break;
            }
            lu_stw_stop(interp);
            remove_tools(code, offset, event, 1 << tool);
            lu_stw_start(interp);
        }
    }
    Py_DECREF(arg2_obj);
    return err;
}

int
lu_fire(PyThreadState *ts, int event, struct lu_frame *frame, lu_codeunit *instr)
{
    PyObject *args[3] = { NULL, NULL, NULL };
    return call_instrumentation_vector(instr, ts, event, frame, instr, 2, args);
}

int
lu_fire_arg(PyThreadState *ts, int event, struct lu_frame *frame, lu_codeunit *instr, PyObject *arg)
{
    PyObject *args[4] = { NULL, NULL, NULL, arg };
    return call_instrumentation_vector(instr, ts, event, frame, instr, 3, args);
}

int
lu_fire_2args(PyThreadState *ts, int event, struct lu_frame *frame, lu_codeunit *instr,
              PyObject *arg0, PyObject *arg1)
{
    PyObject *args[5] = { NULL, NULL, NULL, arg0, arg1 };
    return call_instrumentation_vector(instr, ts, event, frame, instr, 4, args);
}

lu_codeunit *
lu_fire_jump(lu_codeunit *instr, PyThreadState *ts, int event, struct lu_frame *frame,
             lu_codeunit *src, lu_codeunit *dest)
{
    PyObject *to_obj = PyLong_FromLong((long)(dest - lu_code_start(frame->code)) * (long)sizeof(lu_codeunit));
    if (to_obj == NULL) {
        return NULL;
    }
    PyObject *args[4] = { NULL, NULL, NULL, to_obj };
    lu_codeunit *saved = frame->instr;
    int err = call_instrumentation_vector(instr, ts, event, frame, src, 3, args);
    Py_DECREF(to_obj);
    if (err) {
        return NULL;
    }
    if (frame->instr != saved) {
        return frame->instr;            /* the callback moved the frame (f_lineno) */
    }
    return dest;
}

void
lu_fire_exc2(PyThreadState *ts, int event, struct lu_frame *frame, lu_codeunit *instr,
             PyObject *arg0, PyObject *arg1)
{
    PyObject *args[5] = { NULL, NULL, NULL, arg0, arg1 };
    PyObject *exc = PyErr_GetRaisedException();
    int err = call_instrumentation_vector(instr, ts, event, frame, instr, 4, args);
    if (err) {
        Py_XDECREF(exc);
    }
    else {
        PyErr_SetRaisedException(exc);
    }
}

int
lu_instrumentation_line(PyCodeObject *code, int index)
{
    return compute_line(code, get_line_delta(code->_co_monitoring->lines, index));
}

static int
instrumentation_get_line(PyCodeObject *code, lu_line_data *line_data, int index)
{
    return compute_line(code, get_line_delta(line_data, index));
}

int
lu_fire_line(PyThreadState *ts, struct lu_frame *frame, lu_codeunit *instr, lu_codeunit *prev)
{
    PyCodeObject *code = frame->code;
    lu_codeunit *bytecode = lu_code_start(code);
    int i = (int)(instr - bytecode);
    lu_monitoring *monitoring = code->_co_monitoring;
    lu_line_data *line_data = monitoring->lines;
    PyInterpreterState *interp = ts->interp;
    int line = instrumentation_get_line(code, line_data, i);
    int prev_index = (int)(prev - bytecode);
    int prev_line = instrumentation_get_line(code, line_data, prev_index);
    if (prev_line == line) {
        int prev_opcode = bytecode[prev_index].op.code;
        /* RESUME and INSTRUMENTED_RESUME work the instrumentation: never hidden
           by an INSTRUMENTED_LINE. */
        if (prev_opcode != RESUME && prev_opcode != INSTRUMENTED_RESUME) {
            goto done;
        }
    }
    uint8_t tools = monitoring->line_tools != NULL
        ? monitoring->line_tools[i]
        : (interp->monitors.tools[PY_MONITORING_EVENT_LINE]
           | monitoring->local_monitors.tools[PY_MONITORING_EVENT_LINE]);
    /* sys.settrace: call it directly, not boxing the line number only to unbox it. */
    if (tools & (1 << LU_TOOL_SYS_TRACE)) {
        if (ts->c_tracefunc != NULL) {
            PyObject *frame_obj = lu_frame_object(frame);
            if (frame_obj == NULL) {
                return -1;
            }
            if (lu_frame_trace_lines(frame_obj)) {
                int old_what = ts->what_event;
                ts->what_event = PY_MONITORING_EVENT_LINE;
                ts->tracing++;
                int err = ts->c_tracefunc(ts->c_traceobj, (PyFrameObject *)frame_obj, PyTrace_LINE,
                                          Py_None);
                ts->tracing--;
                ts->what_event = old_what;
                Py_DECREF(frame_obj);
                if (err) {
                    return -1;
                }
            }
            else {
                Py_DECREF(frame_obj);
            }
        }
        tools &= (255 - (1 << LU_TOOL_SYS_TRACE));
    }
    if (tools == 0) {
        goto done;
    }
    PyObject *line_obj = PyLong_FromLong(line);
    if (line_obj == NULL) {
        return -1;
    }
    PyObject *args[3] = { NULL, (PyObject *)code, line_obj };
    do {
        int tool = most_significant_bit(tools);
        tools &= ~(1 << tool);
        int res = call_one_instrument(interp, ts, &args[1], 2 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                                      tool, PY_MONITORING_EVENT_LINE);
        if (res < 0) {
            Py_DECREF(line_obj);
            return -1;
        }
        if (res > 0) {
            lu_stw_stop(interp);
            remove_line_tools(code, i, 1 << tool);
            lu_stw_start(interp);
        }
    } while (tools);
    Py_DECREF(line_obj);
done:;
    uint8_t original_opcode = get_original_opcode(line_data, i);
    if (instr->op.code == INSTRUMENTED_INSTRUCTION) {
        /* A LINE callback may have disabled the last LINE tool while leaving
           INSTRUCTION monitoring on. */
        original_opcode = INSTRUMENTED_INSTRUCTION;
    }
    return original_opcode;
}

int
lu_fire_instruction(PyThreadState *ts, struct lu_frame *frame, lu_codeunit *instr)
{
    PyCodeObject *code = frame->code;
    int offset = (int)(instr - lu_code_start(code));
    lu_monitoring *data = code->_co_monitoring;
    int next_opcode = data->per_instruction_opcodes[offset];
    if (ts->tracing) {
        return next_opcode;
    }
    PyInterpreterState *interp = ts->interp;
    uint8_t tools = data->per_instruction_tools != NULL
        ? data->per_instruction_tools[offset]
        : (interp->monitors.tools[PY_MONITORING_EVENT_INSTRUCTION]
           | data->local_monitors.tools[PY_MONITORING_EVENT_INSTRUCTION]);
    PyObject *offset_obj = PyLong_FromLong(offset * (long)sizeof(lu_codeunit));
    if (offset_obj == NULL) {
        return -1;
    }
    PyObject *args[3] = { NULL, (PyObject *)code, offset_obj };
    while (tools) {
        int tool = most_significant_bit(tools);
        tools &= ~(1 << tool);
        int res = call_one_instrument(interp, ts, &args[1], 2 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                                      tool, PY_MONITORING_EVENT_INSTRUCTION);
        if (res < 0) {
            Py_DECREF(offset_obj);
            return -1;
        }
        if (res > 0) {
            lu_stw_stop(interp);
            remove_per_instruction_tools(code, offset, 1 << tool);
            lu_stw_start(interp);
        }
    }
    Py_DECREF(offset_obj);
    return next_opcode;
}

/* Building the monitoring data */

static void
initialize_tools(PyCodeObject *code)
{
    uint8_t *tools = code->_co_monitoring->tools;
    int code_len = (int)Py_SIZE(code);
    for (int i = 0; i < code_len; i++) {
        lu_codeunit *instr = &lu_code_start(code)[i];
        int opcode = instr->op.code;
        if (opcode == INSTRUMENTED_LINE) {
            opcode = get_original_opcode(code->_co_monitoring->lines, i);
        }
        if (opcode == INSTRUMENTED_INSTRUCTION) {
            opcode = code->_co_monitoring->per_instruction_opcodes[i];
        }
        int instrumented = is_instrumented(opcode);
        if (instrumented) {
            opcode = DE_INSTRUMENT[opcode];
        }
        opcode = lu_opcode_deopt(opcode);
        if (opcode_has_event(opcode)) {
            if (instrumented) {
                int event = opcode == RESUME ? instr->op.arg != 0 : EVENT_FOR_OPCODE[opcode];
                tools[i] = code->_co_monitoring->active_monitors.tools[event];
            }
            else {
                tools[i] = 0;
            }
        }
        i += lu_opcode_caches[opcode];
    }
}

static void
initialize_lines(lu_line_data *line_data, PyCodeObject *code, int bytes_per_entry)
{
    line_data->bytes_per_entry = bytes_per_entry;
    int code_len = (int)Py_SIZE(code);
    struct addr_range range;
    range_init(code, &range);
    int current_line = -1;
    for (int i = 0; i < code_len; ) {
        int opcode = lu_base_opcode(code, i);
        int line = range_line(i * (int)sizeof(lu_codeunit), &range);
        set_line_delta(line_data, i, compute_line_delta(code, line));
        int length = instruction_length(code, i);
        if (i < code->_co_firsttraceable) {
            set_original_opcode(line_data, i, 0);
        }
        else {
            switch (opcode) {
            case END_ASYNC_FOR:
            case END_FOR:
            case END_SEND:
            case RESUME:
            case POP_ITER:
                /* END_FOR cannot start a line, as FOR_ITER skips it, nor can END_SEND, as
                   SEND does; RESUME and POP_ITER must not be instrumented as lines. */
                set_original_opcode(line_data, i, 0);
                break;
            default:
                /* The opcode, when the instruction starts a line and so is instrumented. */
                if (line != current_line && line >= 0) {
                    set_original_opcode(line_data, i, opcode);
                }
                else {
                    set_original_opcode(line_data, i, 0);
                }
                current_line = line;
            }
        }
        for (int j = 1; j < length; j++) {
            set_original_opcode(line_data, i + j, 0);
            set_line_delta(line_data, i + j, NO_LINE);
        }
        i += length;
    }
    for (int i = code->_co_firsttraceable; i < code_len; ) {
        int opcode = lu_base_opcode(code, i);
        int oparg = 0;
        lu_codeunit inst = lu_code_start(code)[i];
        while (opcode == EXTENDED_ARG) {
            oparg = (oparg << 8) | inst.op.arg;
            i++;
            inst = lu_code_start(code)[i];
            opcode = lu_base_opcode(code, i);
        }
        oparg = (oparg << 8) | inst.op.arg;
        i += instruction_length(code, i);
        int target;
        switch (opcode) {
        case POP_JUMP_IF_FALSE:
        case POP_JUMP_IF_TRUE:
        case POP_JUMP_IF_NONE:
        case POP_JUMP_IF_NOT_NONE:
        case JUMP_FORWARD:
            target = i + oparg;
            break;
        case FOR_ITER:
        case SEND:
            /* Skip END_FOR and END_SEND. */
            target = i + oparg + 1;
            break;
        case JUMP_BACKWARD:
        case JUMP_BACKWARD_NO_INTERRUPT:
            target = i - oparg;
            break;
        default:
            continue;
        }
        if (get_line_delta(line_data, target) != NO_LINE) {
            int target_opcode = lu_base_opcode(code, target);
            if (target_opcode != POP_ITER) {
                set_original_opcode(line_data, target, target_opcode);
            }
        }
    }
    /* Exception handlers start lines. */
    const uint8_t *start = (const uint8_t *)PyBytes_AS_STRING(code->co_exceptiontable);
    const uint8_t *end = start + PyBytes_GET_SIZE(code->co_exceptiontable);
    const uint8_t *scan = start;
    while (scan < end) {
        int start_offset, size, handler, depth_and_lasti;
        scan = parse_varint(scan, &start_offset);
        scan = parse_varint(scan, &size);
        scan = parse_varint(scan, &handler);
        scan = parse_varint(scan, &depth_and_lasti);
        int original_opcode = lu_base_opcode(code, handler);
        /* END_ASYNC_FOR marks the end of an `async for` and generates no line. */
        if (get_line_delta(line_data, handler) != NO_LINE && original_opcode != END_ASYNC_FOR) {
            set_original_opcode(line_data, handler, original_opcode);
        }
    }
}

static int
allocate_instrumentation_data(PyCodeObject *code)
{
    if (code->_co_monitoring == NULL) {
        lu_monitoring *monitoring = PyMem_Calloc(1, sizeof(lu_monitoring));
        if (monitoring == NULL) {
            PyErr_NoMemory();
            return -1;
        }
        __atomic_store_n(&code->_co_monitoring, monitoring, __ATOMIC_RELEASE);
    }
    return 0;
}

static int
update_instrumentation_data(PyCodeObject *code, PyInterpreterState *interp)
{
    int code_len = (int)Py_SIZE(code);
    if (allocate_instrumentation_data(code)) {
        return -1;
    }
    lu_monitoring *data = code->_co_monitoring;
    /* Local monitors set before a tool was cleared are out of date. */
    for (int i = 0; i < LU_MON_TOOL_IDS; i++) {
        if (data->tool_versions[i] != interp->monitoring_tool_versions[i]) {
            for (int j = 0; j < LU_MON_LOCAL_EVENTS; j++) {
                data->local_monitors.tools[j] &= ~(1 << i);
            }
        }
    }
    lu_local_monitors all_events = local_union(interp->monitors, data->local_monitors);
    int multitools = multiple_tools(&all_events);
    if (data->tools == NULL && multitools) {
        uint8_t *tools = PyMem_Malloc(code_len);
        if (tools == NULL) {
            PyErr_NoMemory();
            return -1;
        }
        data->tools = tools;
        initialize_tools(code);
    }
    if (all_events.tools[PY_MONITORING_EVENT_LINE]) {
        if (data->lines == NULL) {
            struct addr_range range;
            range_init(code, &range);
            int max_line = code->co_firstlineno + 1;
            for (int i = code->_co_firsttraceable; i < code_len; ) {
                int line = range_line(i * (int)sizeof(lu_codeunit), &range);
                if (line > max_line) {
                    max_line = line;
                }
                i += instruction_length(code, i);
            }
            int bytes_per_entry;
            int max_delta = max_line - code->co_firstlineno;
            /* The table holds delta + 2, so 253 is the most for one byte. */
            if (max_delta < 256 + NO_LINE) {
                bytes_per_entry = 2;
            }
            else if (max_delta < (1 << 16) + NO_LINE) {
                bytes_per_entry = 3;
            }
            else if (max_delta < (1 << 24) + NO_LINE) {
                bytes_per_entry = 4;
            }
            else {
                bytes_per_entry = 5;
            }
            lu_line_data *lines = PyMem_Malloc(1 + (size_t)code_len * bytes_per_entry);
            if (lines == NULL) {
                PyErr_NoMemory();
                return -1;
            }
            initialize_lines(lines, code, bytes_per_entry);
            __atomic_store_n(&data->lines, lines, __ATOMIC_RELEASE);
        }
        if (multitools && data->line_tools == NULL) {
            uint8_t *line_tools = PyMem_Malloc(code_len);
            if (line_tools == NULL) {
                PyErr_NoMemory();
                return -1;
            }
            memset(line_tools, all_events.tools[PY_MONITORING_EVENT_LINE], code_len);
            data->line_tools = line_tools;
        }
    }
    if (all_events.tools[PY_MONITORING_EVENT_INSTRUCTION]) {
        if (data->per_instruction_opcodes == NULL) {
            uint8_t *opcodes = PyMem_Malloc(code_len);
            if (opcodes == NULL) {
                PyErr_NoMemory();
                return -1;
            }
            /* Every unit's opcode, so that if local events change while another task
               executes, we know what the original was. */
            for (int i = 0; i < code_len; i++) {
                opcodes[i] = lu_opcode_deopt(lu_code_start(code)[i].op.code);
            }
            data->per_instruction_opcodes = opcodes;
        }
        if (multitools && data->per_instruction_tools == NULL) {
            uint8_t *tools = PyMem_Calloc(code_len, 1);
            if (tools == NULL) {
                PyErr_NoMemory();
                return -1;
            }
            data->per_instruction_tools = tools;
        }
    }
    return 0;
}

static int
force_instrument(PyCodeObject *code, PyInterpreterState *interp)
{
    int code_len = (int)Py_SIZE(code);
    /* Kernel code and shim code never fire events, so they need no data. */
    if (code->co_flags & (LU_CO_KERNEL | LU_CO_HIDDEN | LU_CO_NO_MONITORING_EVENTS)) {
        __atomic_store_n(&code->_co_instrumentation_version, global_version(interp), __ATOMIC_RELEASE);
        return 0;
    }
    if (update_instrumentation_data(code, interp)) {
        return -1;
    }
    lu_local_monitors active_events = local_union(interp->monitors, code->_co_monitoring->local_monitors);
    lu_local_monitors new_events, removed_events;
    int restarted = interp->last_restart_version > code->_co_instrumentation_version;
    if (restarted) {
        removed_events = code->_co_monitoring->active_monitors;
        new_events = active_events;
    }
    else {
        removed_events = monitors_sub(code->_co_monitoring->active_monitors, active_events);
        new_events = monitors_sub(active_events, code->_co_monitoring->active_monitors);
    }
    code->_co_monitoring->active_monitors = active_events;
    if (monitors_are_empty(new_events) && monitors_are_empty(removed_events)) {
        goto done;
    }
    /* Insert instrumentation. */
    for (int i = code->_co_firsttraceable; i < code_len; i += instruction_length(code, i)) {
        int base_opcode = lu_base_opcode(code, i);
        if (opcode_has_event(base_opcode)) {
            int event = base_opcode == RESUME ? lu_code_start(code)[i].op.arg > 0
                                              : EVENT_FOR_OPCODE[base_opcode];
            uint8_t removed_tools = removed_events.tools[event];
            if (removed_tools) {
                remove_tools(code, i, event, removed_tools);
            }
            uint8_t new_tools = new_events.tools[event];
            if (new_tools) {
                add_tools(code, i, event, new_tools);
            }
        }
    }
    /* Remove both the line and the instruction instrumentation before adding
       new ones, or the new could be removed. */
    uint8_t removed_line_tools = removed_events.tools[PY_MONITORING_EVENT_LINE];
    uint8_t removed_per_instruction_tools = removed_events.tools[PY_MONITORING_EVENT_INSTRUCTION];
    if (removed_line_tools) {
        lu_line_data *line_data = code->_co_monitoring->lines;
        for (int i = code->_co_firsttraceable; i < code_len; i += instruction_length(code, i)) {
            if (get_original_opcode(line_data, i)) {
                remove_line_tools(code, i, removed_line_tools);
            }
        }
    }
    if (removed_per_instruction_tools) {
        for (int i = code->_co_firsttraceable; i < code_len; i += instruction_length(code, i)) {
            int opcode = lu_base_opcode(code, i);
            if (opcode == RESUME || opcode == END_FOR) {
                continue;
            }
            remove_per_instruction_tools(code, i, removed_per_instruction_tools);
        }
    }
    uint8_t new_line_tools = new_events.tools[PY_MONITORING_EVENT_LINE];
    uint8_t new_per_instruction_tools = new_events.tools[PY_MONITORING_EVENT_INSTRUCTION];
    if (new_line_tools) {
        lu_line_data *line_data = code->_co_monitoring->lines;
        for (int i = code->_co_firsttraceable; i < code_len; i += instruction_length(code, i)) {
            if (get_original_opcode(line_data, i)) {
                add_line_tools(code, i, new_line_tools);
            }
        }
    }
    if (new_per_instruction_tools) {
        for (int i = code->_co_firsttraceable; i < code_len; i += instruction_length(code, i)) {
            int opcode = lu_base_opcode(code, i);
            if (opcode == RESUME || opcode == END_FOR) {
                continue;
            }
            add_per_instruction_tools(code, i, new_per_instruction_tools);
        }
    }
done:
    __atomic_store_n(&code->_co_instrumentation_version, global_version(interp), __ATOMIC_RELEASE);
    return 0;
}

static int
instrument_lock_held(PyCodeObject *code, PyInterpreterState *interp)
{
    if (is_version_up_to_date(code, interp)) {
        return 0;
    }
    return force_instrument(code, interp);
}

int
lu_instrument(PyCodeObject *code, PyInterpreterState *interp)
{
    int res;
    Py_BEGIN_CRITICAL_SECTION(code);
    PyMutex_Lock(&_PyRuntime.specialize_lock);      /* the specializer rewrites opcodes too */
    res = instrument_lock_held(code, interp);
    PyMutex_Unlock(&_PyRuntime.specialize_lock);
    Py_END_CRITICAL_SECTION();
    return res;
}

#define C_RETURN_EVENTS ((1 << PY_MONITORING_EVENT_C_RETURN) | (1 << PY_MONITORING_EVENT_C_RAISE))
#define C_CALL_EVENTS (C_RETURN_EVENTS | (1 << PY_MONITORING_EVENT_CALL))

/* With the world stopped: every code object a task is running, so that loops
   that never reach RESUME again are instrumented too. */
static int
instrument_all_executing_code_objects(PyInterpreterState *interp)
{
    for (PyThreadState *ts = interp->tasks; ts != NULL; ts = ts->next) {
        for (struct lu_frame *f = (struct lu_frame *)ts->current_frame; f != NULL; f = f->previous) {
            if (f->owner == LU_FRAME_C_ENTRY || f->code == NULL) {
                continue;
            }
            if (instrument_lock_held(f->code, interp)) {
                return -1;
            }
        }
    }
    return 0;
}

static void
set_events(lu_global_monitors *m, int tool_id, lu_event_set events)
{
    for (int e = 0; e < LU_MON_UNGROUPED_EVENTS; e++) {
        uint8_t *tools = &m->tools[e];
        int active = (events >> e) & 1;
        *tools &= ~(1 << tool_id);
        *tools |= (active << tool_id);
    }
}

static void
set_local_events(lu_local_monitors *m, int tool_id, lu_event_set events)
{
    for (int e = 0; e < LU_MON_LOCAL_EVENTS; e++) {
        uint8_t *tools = &m->tools[e];
        int val = (events >> e) & 1;
        *tools &= ~(1 << tool_id);
        *tools |= (val << tool_id);
    }
}

static int
check_tool(PyInterpreterState *interp, int tool_id)
{
    if (tool_id < LU_TOOL_SYS_PROFILE && interp->monitoring_tool_names[tool_id] == NULL) {
        PyErr_Format(PyExc_ValueError, "tool %d is not in use", tool_id);
        return -1;
    }
    return 0;
}

int
lu_monitoring_set_events(int tool_id, lu_event_set events)
{
    PyInterpreterState *interp = PyInterpreterState_Get();
    if (check_tool(interp, tool_id)) {
        return -1;
    }
    if (get_events(&interp->monitors, tool_id) == events) {
        return 0;
    }
    uint32_t new_version = global_version(interp) + MONITORING_VERSION_INCREMENT;
    if (new_version == 0) {
        PyErr_Format(PyExc_OverflowError, "events set too many times");
        return -1;
    }
    set_events(&interp->monitors, tool_id, events);
    set_global_version(interp, new_version);
    return instrument_all_executing_code_objects(interp);
}

int
lu_monitoring_set_local_events(PyCodeObject *code, int tool_id, lu_event_set events)
{
    PyInterpreterState *interp = PyInterpreterState_Get();
    if (code->_co_firsttraceable >= Py_SIZE(code)) {
        PyErr_Format(PyExc_SystemError, "cannot instrument shim code object '%U'", code->co_name);
        return -1;
    }
    if (check_tool(interp, tool_id)) {
        return -1;
    }
    if (allocate_instrumentation_data(code)) {
        return -1;
    }
    code->_co_monitoring->tool_versions[tool_id] = interp->monitoring_tool_versions[tool_id];
    lu_local_monitors *local = &code->_co_monitoring->local_monitors;
    if (get_local_events(local, tool_id) == events) {
        return 0;
    }
    set_local_events(local, tool_id, events);
    return force_instrument(code, interp);
}

int
lu_monitoring_clear_tool(int tool_id)
{
    PyInterpreterState *interp = PyInterpreterState_Get();
    for (int i = 0; i < LU_MON_EVENTS; i++) {
        PyObject *func = lu_monitoring_register_callback(tool_id, i, NULL);
        Py_XDECREF(func);
    }
    lu_stw_stop(interp);
    if (lu_monitoring_set_events(tool_id, 0) < 0) {
        lu_stw_start(interp);
        return -1;
    }
    uint32_t version = global_version(interp) + MONITORING_VERSION_INCREMENT;
    if (version == 0) {
        PyErr_Format(PyExc_OverflowError, "events set too many times");
        lu_stw_start(interp);
        return -1;
    }
    /* This version invalidates the local events of the tool on every code
       object, and is ready for the next set_local_events. */
    interp->monitoring_tool_versions[tool_id] = version;
    set_global_version(interp, version);
    int res = instrument_all_executing_code_objects(interp);
    lu_stw_start(interp);
    return res;
}

/* sys.monitoring.restart_events: DISABLEd events fire again. The restart
   version must exceed every code's version and stay below the current one. */
int
lu_monitoring_restart(void)
{
    PyInterpreterState *interp = PyInterpreterState_Get();
    lu_stw_stop(interp);
    uint32_t restart_version = global_version(interp) + MONITORING_VERSION_INCREMENT;
    uint32_t new_version = restart_version + MONITORING_VERSION_INCREMENT;
    if (new_version <= MONITORING_VERSION_INCREMENT) {
        lu_stw_start(interp);
        PyErr_Format(PyExc_OverflowError, "events set too many times");
        return -1;
    }
    interp->last_restart_version = restart_version;
    set_global_version(interp, new_version);
    int res = instrument_all_executing_code_objects(interp);
    lu_stw_start(interp);
    return res;
}

void
lu_monitoring_free(PyCodeObject *code)
{
    lu_monitoring *m = code->_co_monitoring;
    if (m != NULL) {
        PyMem_Free(m->tools);
        PyMem_Free(m->lines);
        PyMem_Free(m->line_tools);
        PyMem_Free(m->per_instruction_opcodes);
        PyMem_Free(m->per_instruction_tools);
        PyMem_Free(m);
        code->_co_monitoring = NULL;
    }
}

/* The exception events (Python/ceval.c) */

static int
do_monitor_exc(PyThreadState *ts, struct lu_frame *frame, lu_codeunit *instr, int event)
{
    if (frame->code->co_flags & (LU_CO_KERNEL | LU_CO_HIDDEN | LU_CO_NO_MONITORING_EVENTS)) {
        return 0;
    }
    PyObject *exc = PyErr_GetRaisedException();
    int err = lu_fire_arg(ts, event, frame, instr, exc);
    if (err == 0) {
        PyErr_SetRaisedException(exc);
    }
    else {
        Py_DECREF(exc);
    }
    return err;
}

static int
no_tools_for_local_event(PyThreadState *ts, struct lu_frame *frame, int event)
{
    lu_monitoring *data = frame->code->_co_monitoring;
    return data != NULL ? data->active_monitors.tools[event] == 0
                        : lu_no_tools_for_global_event(ts, event);
}

void
lu_monitor_raise(PyThreadState *ts, struct lu_frame *frame, lu_codeunit *instr)
{
    if (!lu_no_tools_for_global_event(ts, PY_MONITORING_EVENT_RAISE)) {
        do_monitor_exc(ts, frame, instr, PY_MONITORING_EVENT_RAISE);
    }
}

void
lu_monitor_reraise(PyThreadState *ts, struct lu_frame *frame, lu_codeunit *instr)
{
    if (!lu_no_tools_for_global_event(ts, PY_MONITORING_EVENT_RERAISE)) {
        do_monitor_exc(ts, frame, instr, PY_MONITORING_EVENT_RERAISE);
    }
}

int
lu_monitor_stop_iteration(PyThreadState *ts, struct lu_frame *frame, lu_codeunit *instr, PyObject *value)
{
    if (no_tools_for_local_event(ts, frame, PY_MONITORING_EVENT_STOP_ITERATION)) {
        return 0;
    }
    PyErr_SetObject(PyExc_StopIteration, value);
    int res = do_monitor_exc(ts, frame, instr, PY_MONITORING_EVENT_STOP_ITERATION);
    if (res < 0) {
        return res;
    }
    PyErr_SetRaisedException(NULL);
    return 0;
}

void
lu_monitor_unwind(PyThreadState *ts, struct lu_frame *frame, lu_codeunit *instr)
{
    if (!lu_no_tools_for_global_event(ts, PY_MONITORING_EVENT_PY_UNWIND)) {
        do_monitor_exc(ts, frame, instr, PY_MONITORING_EVENT_PY_UNWIND);
    }
}

int
lu_monitor_handled(PyThreadState *ts, struct lu_frame *frame, lu_codeunit *instr, PyObject *exc)
{
    if (lu_no_tools_for_global_event(ts, PY_MONITORING_EVENT_EXCEPTION_HANDLED)
        || (frame->code->co_flags & (LU_CO_KERNEL | LU_CO_HIDDEN | LU_CO_NO_MONITORING_EVENTS))) {
        return 0;
    }
    return lu_fire_arg(ts, PY_MONITORING_EVENT_EXCEPTION_HANDLED, frame, instr, exc);
}

void
lu_monitor_throw(PyThreadState *ts, struct lu_frame *frame, lu_codeunit *instr)
{
    if (!lu_no_tools_for_global_event(ts, PY_MONITORING_EVENT_PY_THROW)) {
        do_monitor_exc(ts, frame, instr, PY_MONITORING_EVENT_PY_THROW);
    }
}

/* Callbacks */

/* The legacy BRANCH event: one callback for both BRANCH_LEFT and BRANCH_RIGHT.
   A callable whose self is (handler, right, tool_id). */
static PyObject *
branch_handler(PyObject *self, PyObject *const *args, Py_ssize_t nargs)
{
    PyObject *handler = PyTuple_GET_ITEM(self, 0);
    int right = PyObject_IsTrue(PyTuple_GET_ITEM(self, 1));
    long tool_id = lu_as_long(PyTuple_GET_ITEM(self, 2));
    PyCodeObject *code = (PyCodeObject *)args[0];
    int src_offset = (int)lu_as_long(args[1]);
    if (PyErr_Occurred()) {
        return NULL;
    }
    lu_codeunit instr = lu_code_start(code)[src_offset / 2];
    if (!is_instrumented(instr.op.code)) {
        return Py_NewRef(&lu_instrumentation_disable);        /* already disabled */
    }
    PyObject *res = PyObject_Vectorcall(handler, args, nargs, NULL);
    if (res == &lu_instrumentation_disable) {
        /* FOR_ITER and POP_JUMP_ must be the same size. */
        int offset, other_event;
        int base = lu_base_opcode(code, src_offset / 2);
        if (base == FOR_ITER) {
            if (!right) {
                return res;     /* the POP_ITER cannot be found */
            }
            offset = src_offset / 2;
            other_event = PY_MONITORING_EVENT_BRANCH_LEFT;
        }
        else if (base == POP_JUMP_IF_FALSE || base == POP_JUMP_IF_TRUE || base == POP_JUMP_IF_NONE
                 || base == POP_JUMP_IF_NOT_NONE) {
            if (right) {
                offset = src_offset / 2 + 2;
                other_event = PY_MONITORING_EVENT_BRANCH_LEFT;
            }
            else {
                offset = src_offset / 2;
                other_event = PY_MONITORING_EVENT_BRANCH_RIGHT;
            }
        }
        else {
            return res;         /* an orphaned NOT_TAKEN: the compiler removed its jump */
        }
        PyInterpreterState *interp = PyInterpreterState_Get();
        lu_stw_stop(interp);
        remove_tools(code, offset, other_event, 1 << tool_id);
        lu_stw_start(interp);
    }
    return res;
}

static PyMethodDef branch_handler_def = {
    "branch_event_handler", (PyCFunction)(void (*)(void))branch_handler, METH_FASTCALL, NULL
};

static PyObject *
make_branch_handler(int tool_id, PyObject *handler, int right)
{
    PyObject *self = Py_BuildValue("(OOi)", handler, right ? Py_True : Py_False, tool_id);
    PyObject *f = self != NULL ? PyCFunction_NewEx(&branch_handler_def, self, NULL) : NULL;
    Py_XDECREF(self);
    return f;
}

/* The handler a branch wrapper holds, or the callable itself. */
static PyObject *
unwrap_branch_handler(PyObject *res)
{
    if (res != NULL && PyCFunction_Check(res) && ((PyCFunctionObject *)res)->m_ml == &branch_handler_def) {
        PyObject *handler = Py_NewRef(PyTuple_GET_ITEM(((PyCFunctionObject *)res)->m_self, 0));
        Py_DECREF(res);
        return handler;
    }
    return res;
}

PyObject *
lu_monitoring_register_callback(int tool_id, int event_id, PyObject *obj)
{
    PyInterpreterState *interp = PyInterpreterState_Get();
    PyObject *res;
    if (event_id == PY_MONITORING_EVENT_BRANCH) {
        PyObject *left = NULL, *right = NULL;
        if (obj != NULL) {
            right = make_branch_handler(tool_id, obj, 1);
            left = right != NULL ? make_branch_handler(tool_id, obj, 0) : NULL;
            if (left == NULL) {
                Py_XDECREF(right);
                return NULL;
            }
        }
        lu_stw_stop(interp);
        PyObject *old_right = interp->monitoring_callables[tool_id][PY_MONITORING_EVENT_BRANCH_RIGHT];
        __atomic_store_n(&interp->monitoring_callables[tool_id][PY_MONITORING_EVENT_BRANCH_RIGHT], right,
                         __ATOMIC_RELEASE);
        res = interp->monitoring_callables[tool_id][PY_MONITORING_EVENT_BRANCH_LEFT];
        __atomic_store_n(&interp->monitoring_callables[tool_id][PY_MONITORING_EVENT_BRANCH_LEFT], left,
                         __ATOMIC_RELEASE);
        lu_stw_start(interp);
        Py_XDECREF(old_right);
    }
    else {
        Py_XINCREF(obj);
        lu_stw_stop(interp);
        res = interp->monitoring_callables[tool_id][event_id];
        __atomic_store_n(&interp->monitoring_callables[tool_id][event_id], obj, __ATOMIC_RELEASE);
        lu_stw_start(interp);
    }
    return unwrap_branch_handler(res);
}
