/* PEP 669 instrumentation (runtime/interp/instrument.c, Python/instrumentation.c
   and Include/internal/pycore_instruments.h of CPython 3.14): the tools that
   watch each event, the per-code monitoring data, the instrumented opcodes,
   and the calls the eval loop makes to fire events. */
#ifndef LU_INSTRUMENT_H
#define LU_INSTRUMENT_H

#include "interp/frame.h"

typedef uint32_t lu_event_set;

/* Tool ids: PEP 669's, then the two sys.setprofile and sys.settrace use. */
#define LU_TOOL_DEBUGGER 0
#define LU_TOOL_COVERAGE 1
#define LU_TOOL_PROFILER 2
#define LU_TOOL_OPTIMIZER 5
#define LU_TOOL_SYS_PROFILE 6
#define LU_TOOL_SYS_TRACE 7

/* Ancillary data for line events: one entry per code unit, holding the
   original opcode (0 when the unit starts no line) and the line's delta from
   co_firstlineno. */
typedef struct {
    uint8_t bytes_per_entry;
    uint8_t data[1];
} lu_line_data;

/* Allocated when a code object is first instrumented. Its arrays are only
   ever added, never replaced, so a running task may read them while another
   instruments the same code (under the code's critical section, or with the
   world stopped); the pointers are published with release stores. */
typedef struct lu_monitoring {
    lu_local_monitors local_monitors;   /* sys.monitoring.set_local_events */
    lu_local_monitors active_monitors;  /* global or local: what the bytecode shows */
    uint8_t *tools;                     /* per unit, once two tools share an event */
    uintptr_t tool_versions[LU_MON_TOOL_IDS];
    lu_line_data *lines;
    uint8_t *line_tools;
    uint8_t *per_instruction_opcodes;
    uint8_t *per_instruction_tools;
} lu_monitoring;

#define LU_CO_NO_MONITORING_EVENTS 0x2000000    /* CO_NO_MONITORING_EVENTS */

/* The opcode of unit `i` with instrumentation and specialization stripped. */
int lu_base_opcode(PyCodeObject *code, Py_ssize_t i);
/* The instruction at `at` is INSTRUMENTED_LINE or INSTRUMENTED_INSTRUCTION:
   its cache words, found from the opcode it stands for. */
int lu_instrumented_caches(PyCodeObject *code, lu_codeunit *at);
/* An opcode with specialization removed. */
int lu_opcode_deopt(int opcode);

/* Brings `code` to the interpreter's monitoring version (RESUME). */
int lu_instrument(PyCodeObject *code, PyInterpreterState *interp);
/* Frees a code object's monitoring data. */
void lu_monitoring_free(PyCodeObject *code);

/* Setting events: the caller has stopped the world. */
int lu_monitoring_set_events(int tool_id, lu_event_set events);
int lu_monitoring_set_local_events(PyCodeObject *code, int tool_id, lu_event_set events);
int lu_monitoring_clear_tool(int tool_id);
PyObject *lu_monitoring_register_callback(int tool_id, int event_id, PyObject *callback);

/* Firing events from the eval loop. `instr` is the instruction that fires;
   the frame's `instr` must be saved after it, and a callback that moved the
   frame (f_lineno) leaves a different frame->instr. */
int lu_fire(PyThreadState *ts, int event, struct lu_frame *f, lu_codeunit *instr);
int lu_fire_arg(PyThreadState *ts, int event, struct lu_frame *f, lu_codeunit *instr, PyObject *arg);
int lu_fire_2args(PyThreadState *ts, int event, struct lu_frame *f, lu_codeunit *instr,
                  PyObject *arg0, PyObject *arg1);
/* Events raised with an exception in flight: it survives unless a callback raises. */
void lu_fire_exc2(PyThreadState *ts, int event, struct lu_frame *f, lu_codeunit *instr,
                  PyObject *arg0, PyObject *arg1);
/* JUMP and BRANCH events: the instruction to run next, or NULL on error. */
lu_codeunit *lu_fire_jump(lu_codeunit *instr, PyThreadState *ts, int event, struct lu_frame *f,
                          lu_codeunit *src, lu_codeunit *dest);
/* INSTRUMENTED_LINE: the opcode to run, or -1. */
int lu_fire_line(PyThreadState *ts, struct lu_frame *f, lu_codeunit *instr, lu_codeunit *prev);
/* INSTRUMENTED_INSTRUCTION: the opcode to run, or -1. */
int lu_fire_instruction(PyThreadState *ts, struct lu_frame *f, lu_codeunit *instr);

/* The exception events of the eval loop (Python/ceval.c monitor_*). */
void lu_monitor_raise(PyThreadState *ts, struct lu_frame *f, lu_codeunit *instr);
void lu_monitor_reraise(PyThreadState *ts, struct lu_frame *f, lu_codeunit *instr);
int lu_monitor_stop_iteration(PyThreadState *ts, struct lu_frame *f, lu_codeunit *instr, PyObject *value);
void lu_monitor_unwind(PyThreadState *ts, struct lu_frame *f, lu_codeunit *instr);
int lu_monitor_handled(PyThreadState *ts, struct lu_frame *f, lu_codeunit *instr, PyObject *exc);
void lu_monitor_throw(PyThreadState *ts, struct lu_frame *f, lu_codeunit *instr);

static inline int
lu_no_tools_for_global_event(PyThreadState *ts, int event)
{
    return ts->interp->monitors.tools[event] == 0;
}

/* sys.monitoring (runtime/interp/monitoring.c). */
PyObject *lu_monitoring_module_new(void);
/* sys.setprofile, sys.settrace and the C-API (runtime/interp/legacy_tracing.c). */
extern PyMethodDef lu_sys_tracing_methods[];

extern PyObject lu_instrumentation_disable, lu_instrumentation_missing;

#endif
