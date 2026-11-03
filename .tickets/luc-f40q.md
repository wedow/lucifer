---
id: luc-f40q
status: open
deps: [luc-o9xh]
links: []
created: 2026-11-03T02:47:56Z
type: feature
priority: 0
parent: luc-vvqv
tags: [runtime, kernel, sonnet]
---
# K1k: exceptions, tracebacks and with

Kernel exception hierarchy in Python filling the static PyExc_* types (all builtin exceptions and warnings, attributes such as args/errno/filename/name/obj, __str__/__repr__, add_note, with_traceback, BaseExceptionGroup/ExceptionGroup with split/subgroup/derive). Unwinding through co_exceptiontable (port of get_exception_handler and the 3.14 unwind), PyTracebackObject chain built per frame (kernel-hidden frames add none), __context__ and __cause__ chaining, __suppress_context__, sys.exception/exc_info state only in the task (current_exception, exc_info). Primitives raise kernel exception instances. Opcodes: RAISE_VARARGS, RERAISE, PUSH_EXC_INFO, POP_EXCEPT, CHECK_EXC_MATCH, CHECK_EG_MATCH, CALL_INTRINSIC_2 (PREP_RERAISE_STAR), WITH_EXCEPT_START, LOAD_SPECIAL, LOAD_COMMON_CONSTANT. Programs end by normal exit or SystemExit; uncaught-exception rendering through traceback is luc-66sy.
Implements: DESIGN 4.4 (exception types), 5.2 (unwinding without C recursion); FOUNDATIONS 2.8 (exception state only in the task).
Files: runtime/interp/exceptions.c, lib/kernel/exceptions.py.
Adds tests/kernel: exceptions.py, with_stmt.py.

Builder: sonnet.

## Acceptance Criteria

make check-kernel PROGRAMS="exceptions with_stmt" (bin/lucifer -I -S on the host-compiled .pyc, 512 KiB RLIMIT_STACK, stdout+stderr+exit equal to cpython-3.14.10t).


- Remove the kernelcheck last-stderr-line relaxation added in K1b once tracebacks exist; compare full stderr again.
