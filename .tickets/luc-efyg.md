---
id: luc-efyg
status: open
deps: []
links: []
created: 2026-11-04T20:36:46Z
type: bug
priority: 1
parent: luc-d5u1
tags: [jit]
---
# P0b: kernel programs under -X lucifer_jit=always: tracing mid-loop, interrupt at a call's return, frame depth

Under the jit lane (every function compiled at its first call) four tests/kernel programs differ from the oracle; the default lane and jit-deopt for the rest pass. trace_midloop and trace_opcodes_threads: sys.settrace / sys.monitoring turned on while a compiled loop runs (same task and another task) is not seen; the poll now compares the monitoring version, so the per-task tracing path (ts->tracing, legacy settrace on one thread) is what is missed. signals_poll: _thread.interrupt_main() inside try/except KeyboardInterrupt raises after the try instead of at the call's return when the kernel function ran as a unit. sys_hooks: a depth observation prints 0 for 2 (sys._getframe or recursion depth through h_call's nested evals). Also fails under jit-deopt: signals_poll, sys_hooks.

## Acceptance Criteria

make check-kernel LANE=jit and LANE=jit-deopt pass all programs.


## Notes

**2026-11-04T22:57:31Z**

Seen once 11-04 (luc-0e07 branch, JIT default): make check-compiler's lucifer driver (bin/lucifer -I -S -c DRIVER over a chunk of the corpus, gc disabled) spun at 100% CPU, 48 MB RSS for 36 min and never finished; the same chunks completed in under 150 s on three reruns. Not reproduced since; unknown whether the JIT is involved.
