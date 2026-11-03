---
id: luc-vvqv
status: open
deps: [luc-dfbu, luc-r6ny]
links: []
created: 2026-11-03T01:39:45Z
type: feature
priority: 0
parent: luc-nsmh
tags: [runtime, kernel, slice]
---
# kernel: interpreter, primitives and Python builtins run host-compiled pyc under -I -S

runtime/interp (non-recursive ceval over 3.14 opcodes with heap frames, exceptions, generators, class creation, native marshal reader, frozen loader), runtime/prim (_lucifer primitives), runtime/thread (task/carrier split, pin counter, degenerate one-task-per-carrier scheduler, lu_park/lu_unpark with deadlines under the parking lot and PyMutex, the blocking-syscall bracket and its lint, -X lucifer_handoff), lib/kernel builtins installed into the static types, startup through the _io port and encodings, tools/freeze (docs/DESIGN.md 4.4, 5, 6.1, 9.3). tests/kernel programs are written first and shown failing.

## Acceptance Criteria

make check-kernel: every tests/kernel program (at least 30, covering the families in docs/DESIGN.md 9.3) run as a host-compiled .pyc with bin/lucifer -I -S under a 512 KiB stack limit matches the cpython-3.14.10t oracle on stdout, stderr and exit status.

- tests/kernel/deep_recursion.py (150,000-deep Python recursion, a 10,000-deep yield-from chain, an exception unwound through 100,000 frames) passes on the oracle and on bin/lucifer under the same stack limit.
- Every tests/kernel program also matches the oracle under -X lucifer_handoff, which hands the task between two carriers at every safepoint through the production park/run-queue/bind path (DESIGN 5.8).
- tests/kernel/handoff_witness.py prints 2 distinct _thread.get_native_id() values under -X lucifer_handoff and 1 without.
- make check rejects any blocking syscall in runtime/ outside the lu_syscall_begin/end bracket.
- Foundations (docs/FOUNDATIONS.md 2): tests/kernel/frames.py (_getframe(1).f_locals write-through, f_lineno/f_lasti, f_back skipping kernel builtins, gi_frame, frame.clear(), tb_frame/tb_lineno) matches the oracle, plain and under -X lucifer_handoff.
- tests/kernel/invalidate.py (warm loop, then mutate a class attribute, a base attribute, __bases__, a global, builtins.len, f.__code__, obj.__class__) and tests/kernel/exc_kernel.py (raises through sorted/dict.__getitem__/map with chaining, sys.exception() in generators, except*) match the oracle.
- tests/kernel/gc_at_poll.py: gc.callbacks fire only at calls/back-edges, in the oracle's order (one poll handler; GC runs only from the GC poll bit).
- tests/kernel/hash_seed.py matches the oracle under PYTHONHASHSEED=12345 and =0; unseeded runs differ and sys.flags.hash_randomization == 1.
- check-kernel passes under -X lucifer_handoff=1 through =8 (seeded park-or-continue); -X lucifer_stress=bogus exits 2 with a message.
- make check-capi: a tests/capi program reads PyLong_Type.tp_dict as NULL after kernel init (the dict lives in interpreter state, as in the oracle) while int.__dict__ works (docs/FOUNDATIONS.md 2.1).
- Register refcount owners when a thread state is bound, and delete the stopgap pthread_create wrapper in runtime/object/brc.c (luc-f56i). check_reused_thread_id still passes.
