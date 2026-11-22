---
id: luc-6b0l
status: open
deps: []
links: []
created: 2026-11-22T22:12:37Z
type: task
priority: 0
parent: luc-orn7
tags: [perf, jit]
---
# threads: serialized mode; no object locks while one thread runs Python at a time

Measured: the lock byte taken and released around every compiled attribute store costs about 32 cycles; stubbing the two emitters makes store-heavy loops 1.7-2.3x faster and a Bounce-shaped loop 1.66x. PyCriticalSection_Begin/End are 2.3% of real-program steady state and 45% of a list comprehension.

Design. The runtime is in one of two states, serial or parallel.
- Serial: at most one task runs Python at a time. Switches happen only where compiled code polls and where C blocks or detaches. No store sequence contains a poll, so compiled code needs no lock byte, no atomics on reference counts it owns, and C critical sections need not lock (a section is already released wherever the task could be switched out). An extension that needs the GIL is safe.
- Parallel: today's protocol.
- Compiled code that leaves out locks records a dependency on the serial state. Entering parallel stops the world and invalidates those units, as any other broken assumption does.
- The state follows the program, never a flag or environment variable. Stages:
  1. Serial while only one task has ever run; the second task's start enters parallel. Lock-free INLINE_STORE/_I/_F, LIST_STORE and the other lock_object sites; critical sections skip the lock in serial.
  2. A run token so several tasks can stay serial; Py_mod_gil honoured (md_gil is recorded and ignored today, sys._is_gil_enabled() answers False): importing a module that needs the GIL enters serial, as CPython re-enables the GIL.
  3. Adaptive: stay serial until tasks wait runnable for the token for a sustained share of time, then parallel; back when the contention is gone.
- Later (own ticket when the idiom table shows it): objects confined to one thread keep the lock-free path in parallel, published by a store barrier.

Acceptance, stage 1: idiom rows store-int-attributes <= 65 cycles, store-object-attribute <= 35, store-float-attribute <= 42 with instructions down; a threaded differential test that starts its second thread mid-loop over shared objects stays correct in every JIT mode; the free-threading corpus and the thread kernel tests unchanged. Full review (locks, races).

