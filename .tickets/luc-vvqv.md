---
id: luc-vvqv
status: closed
deps: [luc-dfbu, luc-r6ny, luc-awzt]
links: []
created: 2026-11-03T01:39:45Z
type: feature
priority: 0
parent: luc-nsmh
tags: [runtime, kernel, slice]
---
# kernel: interpreter, primitives and Python builtins run host-compiled pyc under -I -S

runtime/interp (non-recursive ceval over 3.14 opcodes with heap frames, exceptions, generators, class creation, native marshal reader, frozen loader), runtime/prim (_lucifer primitives), runtime/thread (task/carrier split, pin counter, degenerate one-task-per-carrier scheduler, lu_park/lu_unpark with deadlines under the parking lot and PyMutex, the blocking-syscall bracket and its lint, -X lucifer_handoff), lib/kernel builtins installed into the static types, startup through the _io port and encodings, tools/freeze (docs/DESIGN.md 4.4, 5, 6.1, 9.3). tests/kernel programs are written first and shown failing.

## Plan

Built as 21 sub-tickets (parent luc-vvqv); this ticket closes when luc-awzt (K1u) does. Each sub-ticket's acceptance is `make check-kernel PROGRAMS=...` (or a capi/make target) against the oracle; the 512 KiB stack and the live ORACLE_T run hold from K1a.

| # | Ticket | Model | Depends on |
|---|---|---|---|
| K1a | luc-1dl8 hello: marshal, heap-frame loop, frozen boot, runner | opus | - |
| K1b | luc-dr76 functions, calls, closures | sonnet | K1a |
| K1c | luc-o9xh classes, type installer, slots, attributes | opus | K1b |
| K1d | luc-52bm int and bool | sonnet | K1c |
| K1e | luc-2yau str, f-strings, formatting | sonnet | K1d |
| K1f | luc-d47e float and complex | sonnet | K1d, K1e |
| K1g | luc-fha1 bytes, bytearray, memoryview | sonnet | K1e, K1i |
| K1h | luc-616t tuple, list, range, slice, iteration, sort | sonnet | K1d, K1i |
| K1i | luc-rjvn lu_park, parking lot, PyMutex, critical sections, bracket lint | opus | K1a |
| K1j | luc-r5qm dict, set, comprehensions | sonnet | K1e, K1h, K1i |
| K1k | luc-f40q exceptions, tracebacks, with | sonnet | K1c |
| K1l | luc-5w74 generators, coroutines, deep recursion | opus | K1k, K1h |
| K1m | luc-xljo builtins functions | sonnet | K1f, K1g, K1j, K1l |
| K1n | luc-qidc config, sys, frozen importlib, builtin modules | sonnet | K1m |
| K1o | luc-8dqq _io port, codecs, encodings, std streams | sonnet | K1n, K1i |
| K1p | luc-dlik task binding, foreign threads, refcount owners | opus | K1i |
| K1q | luc-at0e poll reasons, one handler, GC trigger, stop-the-world | opus | K1n, K1p |
| K1r | luc-a0sq -X lucifer_handoff: two carriers, run queue | opus | K1o, K1q, K1p |
| K1s | luc-ahq7 frame objects, f_locals, one frame walker | opus | K1n |
| K1t | luc-abar versions, watchers, inline caches | opus | K1n |
| K1u | luc-awzt opcode closure, remaining programs, all lanes | sonnet | K1r, K1s, K1t |

Parallel lanes: K1i and K1p run beside the type ladder from K1a; after K1c, K1d-K1h and K1k proceed side by side; after K1n, K1o/K1q/K1s/K1t run in parallel.

M:N shapes from K1a: frames on the task's chunked datastack with owner tag and instr, the task record (PyThreadState + tail with pin and pending events), the carrier record with its poll word, one eval-loop activation per task, lu_enter_frame (PEP 523 check), RESUME's monitoring-version compare. All degenerate until K1p-K1r fill them.

## Bootstrap order (DESIGN 4.4)

Native, before any kernel Python (K1a):
1. `_PyRuntime`, the main `PyInterpreterState`, the main carrier (poll word) and main task (datastack). `_Py_HashSecret` from PYTHONHASHSEED or getrandom, before the first hash.
2. Layout constructors and hashes for everything marshal yields or the loop allocates: str, bytes, int, float, complex, tuple, list, frozenset/set (the setobject.c probe, shared later with kernel set), slice, code, function, cell, module, PyCFunction, the singletons and StopIteration. Static types get `tp_mro`/`tp_bases` natively; `_PyType_Lookup` is a primitive.
3. The namespace dict (exact-str keys, `runtime/object/dict.h`), the `_lucifer` module, empty `builtins` and `sys` modules, `sys.modules`.
4. A boot-fatal path: an exception before the kernel is up prints its type and message and exits 1.

Kernel Python, frozen, in order: `lib/kernel/_boot.py` (functions only: `__build_class__`, slotdefs, `@kernel_type`; K1c) → `object`/`type` → numbers, str/bytes, containers, exceptions, descriptors, iterators → `builtins` functions → `sys` population and `lib/modules` ports → `importlib._bootstrap[_external]` install → `encodings` → std streams → run main (K1n, K1o). Uncaught-exception rendering through `traceback` stays with luc-66sy.

## Acceptance coverage

| luc-vvqv acceptance line | Delivered by |
|---|---|
| check-kernel: >= 30 programs, families of DESIGN 9.3, -I -S, 512 KiB, oracle match | runner K1a; programs K1a-K1t; >= 30 and family sweep K1u |
| deep_recursion.py on oracle and lucifer | K1l |
| every program under -X lucifer_handoff | K1r (lane), K1u (all programs) |
| handoff_witness.py 2 vs 1 | K1r |
| make check rejects unbracketed blocking syscalls | K1i |
| frames.py plain and under handoff | K1s (plain), K1u (handoff) |
| invalidate.py | K1t (plain), K1u (handoff) |
| exc_kernel.py | K1m |
| gc_at_poll.py | K1q |
| hash_seed.py at 12345 and 0; unseeded differ; hash_randomization == 1 | K1n |
| handoff=1..8 | K1r, K1u; lucifer_stress=bogus exits 2: K1n |
| capi PyLong_Type.tp_dict NULL | K1c |
| refcount owners at bind; pthread_create wrapper deleted; check_reused_thread_id | K1p |

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

## Notes

**2026-11-03T18:02:57Z**

K1 closed (checked against master plus branch luc-awzt, 2026-11-03). Evidence per coverage-map line:
- >= 30 programs, families of DESIGN 9.3, -I -S, 512 KiB, oracle match: make check-kernel, 477/477 checks, 441 tests/kernel programs, plain lane; includes the new match_stmt, tstrings, scoping, control_flow. Families: int_arith/bigint, str_/bytes_ methods, list/dict/set/tuple, classes, descriptors, exceptions, generators, closures, comprehensions, with_stmt, fstrings, print_args, deep_recursion, handoff_witness all present.
- deep_recursion.py: passes in every lane (plain, handoff, handoff:1..8).
- every program under -X lucifer_handoff: 477/477 in handoff and handoff:1..8 (marshal_dumps is XFAIL, luc-ymb3, as agreed).
- handoff_witness.py 2 vs 1: HANDOFF_STDOUT row in tools/kernelcheck; passes plain and all handoff lanes.
- make check rejects unbracketed blocking syscalls: check-blocking is part of make check, green.
- frames.py plain and under handoff; invalidate.py (plain and handoff); exc_kernel.py; gc_at_poll.py: all in the kernel set, green in all 10 lanes.
- hash_seed.py at 12345 and 0, unseeded differ, hash_randomization == 1: RUNS/SEEDED rows in kernelcheck, green.
- handoff=1..8 and lucifer_stress=bogus exits 2: lanes handoff:1..8 green; FLAG_ROWS stress rows pass.
- capi PyLong_Type.tp_dict NULL: tests/capi/static_tp_dict.c in make check-capi, green.
- refcount owners at bind, pthread_create wrapper deleted, check_reused_thread_id: tests/capi/tstate_bind.c and refcount.c green; no pthread_create left in runtime/object/brc.c.
- make check (blocking, globals, capi, kernel, embed), make abi-audit (1843/1843, data sizes 215/215), make check-compiler (code stage identical on closure, difftest, kernel, lib): green.
- kernelcheck --opcodes: 122 base opcodes, 119 handled, 3 exempt (RESERVED, INTERPRETER_EXIT, BINARY_OP_INPLACE_ADD_UNICODE), 0 unhandled.
