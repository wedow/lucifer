---
id: luc-56fo
status: open
deps: []
links: []
created: 2026-11-07T14:32:49Z
type: bug
priority: 3
---
# Interpreter: a release shows the frame's line as of its last call

The interpreter saves frame->instr (and stackpointer) only at calls, so a finalizer or weakref callback run by a release (STORE_FAST, DELETE_FAST, POP_TOP, a store's old value) reads sys._getframe(1).f_lineno and traceback lines of the last call instead of the releasing instruction; CPython 3.14 shows the releasing line. Compiled code publishes the right instruction since luc-23yk. tests/kernel/jit_release_frames.py puts each release on a line with a call (abs) so the jit-deopt lane, which interprets part of the time, still matches. Fix without a store per instruction on the interpreter's hot path, then drop the workaround in that test.

