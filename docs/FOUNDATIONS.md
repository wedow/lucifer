# Lucifer foundations

Status: audit of record for the runtime shapes that must exist before the code
that would otherwise hard-wire their absence. It complements `docs/DESIGN.md`.
M:N execution (heap frames, the task/carrier split, the pin counter, `lu_park`,
per-carrier stop-the-world, `-X lucifer_handoff`) is DESIGN §5 and is not
restated here. Rows below say where they meet it.

Rule (owner): if the final runtime needs feature X, the shape of X exists from
the start. A shape is a field, one code path or one interface, plus a
binary-level check that fails if the shape is missing. It is not an
implementation of X.

Evidence conventions: CPython issue numbers and versions are from
`Misc/NEWS.d` in the CPython v3.14.0 reference clone. Oracle counts are from
`results/libtest/cpython-3.14.10t.tsv` and `results/numpytest/cpython-3.14.10t.tsv`.
Oracle probes ran `cpython-3.14.10t`. The donor's current docs
(`docs/04-SYSTEM-MAP.md`, `docs/05-INVARIANTS.md`, `docs/vm/*` at donor
`origin/master`) are the record of shapes that work. They are not evidence of
failed retrofits. Claims not checked against a source are marked (unverified).

## 1. Ranking

Ranked by the cost of adding the shape later. "Rung" names the ticket that
carries it (§4 lists the new rungs).

| # | Item | Minimal shape now | Rung |
|---|---|---|---|
| 1 | Per-interpreter state (subinterpreters) | Three state records: `_PyRuntime`, `PyInterpreterState`, task/carrier. No mutable C globals outside an audited list. Static builtin types keep `tp_dict`/`tp_subclasses` per interpreter. Kernel functions are reached through `interp`. | K0.1, K1, K1.1, M1.6a |
| 2 | One frame record and one frame walker | One frame layout (§5.2) with an owner tag, lazy `PyFrameObject`, and a write-through `f_locals`. One walker serves `_getframe`, tracebacks, GC roots, deopt, faulthandler and tracemalloc. | K1, P0 |
| 3 | Instrumentation under all tiers | Instrumented opcodes and per-code monitoring data from K1. Legacy tracing runs on `sys.monitoring`. A monitoring version is checked at `RESUME`, and JIT code depends on it. | K1 (shape), M0.3, P0 |
| 4 | Invalidation: versions and watchers | The ABI version fields carry CPython's meaning, each with one notify path that kernel Python also uses. Internal JIT dependencies are watchers. | K1, M1.3, P0 |
| 5 | Poll reasons and one handler | CPython's 8 eval-breaker bits plus Lucifer's in the poll word (§5.6), with one handler for interpreter and JIT. GC runs only from it. | K1 |
| 6 | One allocation choke point | `PyMemAllocatorEx` tables for the raw, mem and obj domains. The default allocator selects per-(carrier, interpreter) mimalloc heaps. Object creation goes through `_Py_NewReference`/`_Py_Dealloc` with the reftracer hook. | K0.1, M1.1a |
| 7 | Stress modes as runtime flags | One `-X lucifer_stress=` parser. Sites at the allocation choke point, at guard emission and in the poll handler. `-X lucifer_handoff` gains a seed. | K1, M0.2, P0 |
| 8 | Exception state only in the task | `current_exception`/`exc_info` are the only home of an exception. JIT code never keeps one in registers across a C call, and an unwind deopts to the interpreter's table. | K1, M1.2 |
| 9 | `os.fork` with threads | One fork path: stop the world, take every runtime lock from a registry, then reset carrier, BRC and parking-lot state in the child. | K0.1 (registry), park/unpark rung |
| 10 | Startup image readiness | All init-time heap state is rooted in the state records. Init is address-independent and seed-independent. No string hashes in frozen data. | K0.1 (via globals audit), L1 |
| 11 | JIT IR/backend boundary (aarch64) | SSA and the register allocator are target-neutral. One `target.h` interface. Polls, calls, deopt stubs and frame descriptors are per-arch. The interpreter builds for aarch64. | P0, L2 |
| 12 | Observability | Frame pointers in runtime and JIT frames. A perf map and jitdump from the first published code, and `-X perf`. `_Py_DebugOffsets` at the head of `_PyRuntime`. faulthandler reads the frame chain. | K0.1 (flags), M1.1, P0, L3 |
| 13 | Finalization and embedding | `Py_BytesMain` runs PyConfig, then `Py_InitializeFromConfig`, then `Py_RunMain`. `Py_FinalizeEx` follows CPython's order. The runtime is an archive that `bin/lucifer` links. | K0.1 (archive), M0.0 |
| 14 | Weakrefs, finalizers, resurrection under FT | The cp314t `ob_gc_bits` flags, one `PyObject_CallFinalizerFromDealloc` path, weakref clearing under cp314t's striped locks. | M0.2 |
| 15 | Imports, `.pyc`, zipimport, bootstrap | Unmodified `importlib._bootstrap[_external]` and `zipimport` over CPython's `_imp` surface, including the multi-phase slots `Py_mod_multiple_interpreters` and `Py_mod_gil`. | M0.0, M1.2 |
| 16 | Hash secret and seed | `_Py_HashSecret` is set from PyConfig before the first hash. Frozen data carries no hash. `sys.flags.hash_randomization` defaults to 1. | K1 |
| 17 | Audit hooks (PEP 578) | One `PySys_Audit` primitive. Hooks in `_PyRuntime` (C, pre-init) and in `PyInterpreterState` (Python). | M0.0 |
| 18 | PEP 523 frame-eval hook | One frame-entry function that checks `interp->eval_frame`. A non-NULL hook forces interpreter entry for that interpreter. | K1 |

Merged into other rows: GC and JIT virtual objects (2, 7); signals,
`SetAsyncExc` and pending calls (5); `PyThreadState` per subinterpreter, atexit
in subinterpreters, and thread-local state (1, 13); recursion limits per task
(DESIGN §5.2, plus the C-stack check in 11); `gc.freeze` (14: one more
`ob_gc_bits` flag).

Rejected:
- **Stable ABI (abi3) beside cp314t.** 3.14 free-threaded builds do not
  support the limited API. A GIL-built `.abi3.so` assumes the 16-byte
  `PyObject` and would crash on the oracle too. The future abi3t makes
  `PyObject` opaque, which only removes constraints, so nothing needs building
  now.
- **`sys.setswitchinterval`.** It stores a value and has no effect without a GIL.

## 2. Items

Each item gives why a retrofit costs, the minimal shape, the rung, and the
check. Checks run `bin/lucifer` or unmodified extensions. A `-X lucifer_*` flag
is a runtime policy in the production path, never a test-only branch.

### 2.1 Per-interpreter state

**Why.** CPython took a decade to move state out of C globals:
- PEP 554 was filed in 2017, and the experimental
  `--experimental-isolated-subinterpreters` build option was removed in 3.12
  (gh-84694).
- The per-interpreter GIL landed in 3.12 (gh-99113). Its NEWS entry says it
  was "made possible by virtue of the isolated state of each interpreter".
- Making that state isolated took PEP 489 (3.5), PEP 573 (3.9), PEP 630, PEP
  683 (immortals, 3.12) and PEP 687 (3.12).
- `concurrent.interpreters` arrived only in 3.14 (PEP 734).
- In v3.14.0, `Tools/c-analyzer/cpython/globals-to-fix.tsv` still lists 411
  rows.

Since 3.12, a static builtin type's `tp_dict` is NULL in the struct and the dict
lives in interpreter state (`Objects/typeobject.c` `lookup_tp_dict`). The oracle
confirms it: `tp_dict` of `int`, `float` and `list` reads NULL; a heap type's
does not.

The requirement is on the public bar:
`numpy/_core/tests/test_multiarray.py::TestSubinterpreterTeardown::test_subinterpreter_import_fails_cleanly`
is PASS in the oracle manifest. The test needs `concurrent.interpreters.create()`,
`exec`, `ExecutionFailed.excinfo.msg` containing "does not support loading in
subinterpreters" (the `Py_mod_multiple_interpreters` check), and a clean `close()`.

The NumPy wheel imports `PyInterpreterState_GetID` and `PyInterpreterState_Main`.
Oracle libtest has 272 PASS rows in modules dedicated to subinterpreters
(`test__interpreters` 66, `test__interpchannels` 68, `test_crossinterp` 101,
`test_concurrent_futures.test_interpreter_pool` 37), and 217 PASS rows whose
names mention interpreters or subinterpreters. `test_interpreters` (174) and
`test_embed` (84) are SKIP in the oracle.

Lucifer's advantage is that Python-written modules are per-interpreter for
free. Only native state can leak.

**Shape.**

| Record | Holds | Never holds |
|---|---|---|
| `_PyRuntime` (the exported 365,624-byte symbol) | `_Py_DebugOffsets` header (§2.12), interpreter list, allocator domain tables, C audit hooks, runtime lock registry (§2.9), stop sequence | anything a second interpreter must not see |
| `PyInterpreterState` (private layout) | `sys.modules`, builtins, the `managed_static_type_state[]` array (`tp_dict`, `tp_subclasses`, `tp_weaklist` per static type index), the kernel function table, interned non-immortal strings, the BRC bucket table, atexit callbacks, audit hooks, monitoring state, `eval_frame`, GC state, PEP 489 module state | carrier identity |
| Task/carrier (DESIGN §5.3) | as listed there; `interp` is a task field | — |

Rules:
- **Static types and singletons** stay process-wide and immortal. The kernel
  installs methods and slots into the static type, which is shared; the type's
  dict goes into the current interpreter's `managed_static_type_state`. The
  `Py_TPFLAGS_*` "static builtin" flag marks which types use it.
- **Kernel code and functions.** Kernel code objects are immortal and shared.
  Kernel *functions* bind module globals, so they are per interpreter. Slot
  thunks and C-API thunks find them via `tstate->interp->kernel[ID]`, never via
  a C pointer captured at startup.
- **Carrier state is keyed by interpreter.** A carrier may bind tasks of
  different interpreters (`PyThreadState_Swap`). Its mimalloc heaps and BRC
  queue are therefore per (carrier, interpreter), created lazily: one entry in
  the common case. This is a refinement of DESIGN §5.3, which says "per
  carrier". cp314t keeps both per tstate. A GC of interpreter A walks only A's
  heaps, and a BRC merge deallocates under the owning interpreter. Otherwise
  `tp_dealloc` would run against another interpreter's `sys.modules`.
- **`_lucifer` and every native module** use multi-phase init with module state
  and no file-scope mutable statics.
- **`_Thread_local` holds only the carrier word** (DESIGN §5.3).

**Checks.**
- **K0.1** `make check-globals` runs `nm` over `bin/lucifer`. Every writable
  symbol (`D`/`d`/`B`/`b`) must be one of: an exported oracle data symbol, the
  carrier TLS word, a mimalloc process-global, or a row in
  `tools/abi/globals.tsv` with a reason. CPython's `globals-to-fix.tsv` is the
  same method.
- **K1.1** `make check-kernel` passes under `-X lucifer_subinterp`. The flag
  initializes the main interpreter, then creates a second one with
  `Py_NewInterpreterFromConfig` (isolated config), runs the program there, ends
  it, and finalizes. Each interpreter runs the kernel's init once.
- **M1.6a** Oracle PASS rows of `test__interpreters`, `test__interpchannels`,
  `test_crossinterp` and `test_interpreter_pool` pass. A `tests/capi` program
  imports a module declaring `Py_MOD_MULTIPLE_INTERPRETERS_NOT_SUPPORTED` in a
  subinterpreter and prints the oracle's `ExecutionFailed` message.

### 2.2 One frame record, one walker

**Why.**
- **`f_locals`.** CPython's semantics took seven years to fix. PEP 558 (2017)
  was withdrawn for PEP 667, which landed in 3.13 (gh-74929).
- **Frame layout.** 3.11's split of `_PyInterpreterFrame` from `PyFrameObject`
  broke every extension that read frame fields directly.
- **PyPy.** It needed "virtualizables" so that JIT frames could stay unforced
  while still existing on the heap (`rpython/doc/jit/virtualizable.rst`).
- **The donor.** INV-GC-1/INV-EXE-2 make one root walk the authority ("no
  parallel root walker exists"). Its descriptor walk copies native values into
  frame slots before any stop is acknowledged.

Consumers in the bar's lanes:
- `sys._getframe` appears in 36 Lib/test files and 5 NumPy test files.
- regrtest calls `faulthandler.enable(all_threads=True)` and
  `dump_traceback_later` (`libregrtest/setup.py:42`, `single.py:281`).
- NumPy's `test_tracemalloc` (oracle PASS) captures allocation tracebacks.
- NumPy imports `PyThreadState_GetFrame` and `PyFrame_New`.
- Oracle PASS rows: `test_frame` 53, `test_sys` 93, `test_inspect`.

**Shape.** The frame struct is DESIGN §5.2's. These fields and paths are added
to it:
- `owner ∈ {interpreter, generator, jit, kernel-hidden, c-entry}`. Kernel-hidden
  frames are skipped by every consumer (DESIGN §4.4).
- `instr` (the next instruction to execute) is valid at every safepoint and
  every C call, as INV-EXE-1 requires.
- `frame_obj` is a lazy back-pointer to `PyFrameObject`. The object owns
  nothing and reads through the record. After the frame exits it takes a copy
  (CPython's `take_ownership`).
- `FrameLocalsProxy` writes go to the fast-locals array. If the frame is `jit`,
  the write first gives it real slots, and compiled code leaves the frame when
  the call it is in returns, so a register copy never goes stale.
- One `lu_frame_walk(task, visit)` serves `sys._getframe`, traceback creation,
  `PyThreadState_GetFrame`, the GC's deferred-root scan, deopt reconstruction,
  tracemalloc and faulthandler.
  - JIT frames expose their values through per-safepoint descriptors, and
    virtual objects' referents count as roots. Compiled code owns a `jit`
    frame's locals and stack (counted references, so the collector needs
    nothing from it) and its slots hold nothing. Before each call that may
    run Python it publishes the call's exit description and its spill words,
    where the allocator keeps every local the description names for the
    call's duration; after the call it withdraws them. A reader of the
    frame's locals (`lu_frame_slot`) decodes them from there, building a box
    for a raw int or float. The frame's own task can only read it from inside
    such a call. Any other task stops the world first, and every stopped
    task is then either inside such a call or out of compiled code: a
    compiled poll leaves through its exit, which gives the frame back to the
    interpreter. The stop's handshake orders the publication before the read,
    and the owner cannot change the spill words while it is stopped. A frame
    stopped elsewhere in compiled code (a finalizer run by a release) has
    published nothing, and reads as having no bound locals.
  - Links are single-store pushes and pops, so a signal-time reader
    (faulthandler) sees a valid chain with a possibly lagging `instr`. That is
    CPython's guarantee.

**Checks.**
- **K1** `tests/kernel/frames.py` checks:
  - `_getframe(1).f_locals` write-through, visible to the caller;
  - `f_lineno` and `f_lasti` after a call;
  - `f_back` skips kernel builtins (calls through `sorted(key=...)`);
  - `gi_frame` and `frame.clear()`;
  - `tb_frame`/`tb_lineno` on a caught exception.

  It matches the oracle, plain and under `-X lucifer_handoff`.
- **P0** The same program passes under `-X lucifer_jit=always` and under
  `-X lucifer_stress=deopt:1`.

### 2.3 Instrumentation under all tiers

**Why.**
- **PyPy.** Its JIT stopped running under `sys.settrace` until the 2.2
  branches `jit-settrace` and `fix-trace-jit`, three years after the JIT
  shipped (`pypy/doc/whatsnew-2.2.rst`), "necessary to make coverage.py fast".
- **CPython.** The 3.11 specializing interpreter forced PEP 669 in 3.12:
  instrumented opcodes ≥ 234 replace dispatch-loop checks. cProfile moved onto
  it (gh-103533). 3.13 folded the monitoring version into the eval-breaker word
  (gh-109369).

Oracle PASS rows: `test_sys_settrace` 448, `test_monitoring` 89, `test_pdb` 152,
`test_bdb` 37, `test_sys_setprofile` 29, `test_cprofile` 20.

**Shape (K1).**
- The opcode table reserves the instrumented range. The private code tail has
  a monitoring-data pointer and a monitoring version.
- `RESUME` compares the code's version with the interpreter's, using the field
  CPython uses.
- `settrace`/`setprofile` are the legacy-tracing layer over monitoring, as in
  `Python/legacy_tracing.c`, so there is one event source.
- Global event changes are a stop-the-world (DESIGN §5.6). Per-task tracing is
  a targeted event (§5.6).
- JIT code (P0) records the version as a dependency (§2.4), so enabling tracing
  mid-loop invalidates the code and the next poll deopts.

**Checks.**
- **M0.3** libtest `test_sys_settrace`, `test_sys_setprofile` and
  `test_monitoring` reach ≥ 90% of the oracle's PASS rows.
- **M0.3** `tests/kernel/trace_midloop.py` turns on `sys.settrace`, then
  `sys.monitoring` `LINE`/`BRANCH` events, from inside a hot loop on another
  task and in the same task. It matches the oracle.
- **P0** The same PASS sets hold with `-X lucifer_jit=always`.

### 2.4 Invalidation: versions and watchers

**Why.**
- **Dict versions.** CPython added `ma_version_tag` (PEP 509, 3.6), then removed
  it (PEP 699, 3.12) in favour of watchers.
- **Watchers.** Cinder's JIT had to upstream type (gh-91051), dict, function and
  code (gh-91054) watchers into 3.12 to stop patching CPython.
- **`tp_version_tag`.** It is part of the ABI. Its validity rules kept changing:
  gh-89811 (3.13) and gh-119258 (3.14).
- **The donor.** Dependency lists mark code dead (`optimizer_dependencies.inc`,
  DESIGN §6.2.7).

Oracle PASS rows: `test_capi.test_watchers` 58. difftest has
`int_type_change_after_warmup`.

**Shape (K1).**
- **Fields keep CPython's meaning and position:** `tp_version_tag`, and
  `tp_watched` (bits for watcher ids 0–7) in `PyTypeObject`; `_ma_watcher_tag`
  in `PyDictObject`; `func_version` in `PyFunctionObject`.
- **One notify path per kind:** `lu_type_modified` (= `PyType_Modified`,
  recursive over `tp_subclasses`), `lu_dict_notify` (called by the
  namespace-dict primitives, so kernel Python's `dict` code notifies too),
  `lu_func_notify`, and code create/destroy.
- **The JIT's dependencies use the same paths.** They are internal watcher ids,
  as CPython reserves for itself. Inline caches guard on the same fields.

**Checks.**
- **K1** `tests/kernel/invalidate.py` warms a loop, then mutates:
  - a class attribute, a base's attribute and `__bases__`;
  - a global, then `builtins.len`;
  - `f.__code__` and `obj.__class__`.

  Each result matches the oracle.
- **M1.3** All 58 oracle-PASS rows of `test_capi.test_watchers` pass.
- **P0** `invalidate.py` passes with `-X lucifer_jit=always`.

### 2.5 Poll reasons and one handler

DESIGN §5.6 owns the protocol: the per-carrier poll word, the poll sites,
stop-the-world, targeted events and signals in the main task. This item fixes
only what the word means.

**Why.**
- 3.12 moved GC triggering from allocation to the eval breaker (gh-97922), so
  collection runs only at a safe point.
- 3.13 merged the breaker into one atomic word (gh-109369) and made it
  per-thread (gh-112175).
- 3.14 added the remote-debugger request (PEP 768, gh-131591).

Each reason added late meant another check scattered through the loop.

**Shape (K1).**
- The bit layout is CPython's `_PY_*_BIT` set (GIL drop, signals, pending
  calls, async exception, GC scheduled, please-stop, explicit merge, JIT
  invalidate), followed by Lucifer's bits: preempt, deopt-self, stress tick and
  remote exec.
- The upper bits carry the monitoring version, as in 3.13.
- One `lu_poll_slow(tstate)` handles every bit, for the interpreter and the JIT
  alike.
- `gc.collect` thresholds only *set* the GC bit, and `Py_AddPendingCall` sets
  the calls bit.

**Checks.**
- **K1** `tests/kernel/gc_at_poll.py` registers a `gc.callbacks` entry and
  allocates in a loop. It prints the bytecode-level points where callbacks
  fire, matching the oracle's order (collections start only at
  calls/back-edges).
- **M0.0** `tests/kernel/signals.py` covers:
  - `signal.raise_signal(SIGUSR1)` inside a tight `while` loop;
  - `_thread.interrupt_main()`;
  - `signal.setitimer` interrupting `time.sleep`.

  It matches the oracle.
- **M1.3** `PyThreadState_SetAsyncExc` via `_testcapi` (test_threading rows)
  passes.

### 2.6 One allocation choke point

**Why.**
- PEP 445 (3.4) retrofitted domain tables so that PEP 454's tracemalloc could
  exist.
- PEP 683 and the reftracer (`PyRefTracer_SetTracer`, 3.13, gh-93502) added
  hooks at object birth and death. 3.14 still fixed a missed destroy event in
  ceval's inline `Py_DECREF` (gh-130382): a second path was the bug.
- In cp314t, PyMem_ and PyObject_ must be mimalloc (`Objects/obmalloc.c:345`),
  because the GC walks heaps.

NumPy's `test_tracemalloc` (oracle PASS) requires `np.zeros(10**6)` to show
within 1,000 bytes of 1 MB in a snapshot diff. The wheel imports
`PyTraceMalloc_Track`/`Untrack`. Oracle PASS rows: `test_tracemalloc` 51,
`test_capi.test_mem` 18.

**Shape (K0.1).**
- `PyMem_RawMalloc`, `PyMem_Malloc` and `PyObject_Malloc` call through
  `_PyRuntime.allocators.{raw,mem,obj}` (`PyMemAllocatorEx`).
- The default obj and mem contexts pick the current (carrier, interpreter)
  heap: `MEM`, `OBJECT`, `GC` or `GC_PRE`, CPython's four heap ids.
- GC objects enter through one `lu_gc_alloc(type, presize)`, which selects
  `GC`/`GC_PRE`.
- `PyMem_SetAllocator`/`GetAllocator`/`SetupDebugHooks` are real.
- `_Py_NewReference` and `_Py_Dealloc` are the only birth and death paths for
  kernel primitives, C-API, interpreter and JIT materialization. Both test the
  reftracer pointer.
- JIT inline allocation (P0) is emitted only while the obj domain is the
  default and no tracer is set. Installing either invalidates such code.

**Checks.**
- **K0.1** `tests/capi/alloc_hooks.c` (`make check-capi`):
  - wraps all three domains with counting allocators that delegate to the
    previous ones;
  - sees `PyMem_RawMalloc`, `PyMem_Malloc` and `PyObject_Malloc` counted;
  - restores the defaults;
  - sees `PyRefTracer` create and destroy events for an object made by
    `PyObject_Init` and released by `Py_DECREF`.
- **M1.1a** libtest `test_tracemalloc` at oracle parity (51).
- **M1.7** NumPy's `test_tracemalloc` passes (already in the bar).

### 2.7 Stress modes as runtime flags

**Why.**
- **The donor.** It keeps fault injection as a production flag. `--stress
  <seed>` injects timing only: extra GCs, spurious epochs, debug stops,
  redefinition timing. Output must be byte-identical across seeds (INV-E2E-3,
  `docs/vm/e2e-DESIGN.md` §7).
- **The donor's optimizer stress.** `--optimizer-stress seed:interval` forces
  exits at a seeded phase (`vm/optimizer_stress.inc`: "disabled artifacts
  contain no stress instructions").
- **HotSpot and V8.** They ship comparable switches (`-XX:+DeoptimizeALot`,
  `--deopt-every-n-times`, `--gc-interval`) (unverified detail).
- Sites added after the fact miss paths. Sites that exist from the start are
  just more call sites of one parser.

**Shape.**
- **One parser.** `-X lucifer_stress=KIND[:N][,…][,seed:S]` sets flags in
  `_PyRuntime` at config time. Unknown kinds are a startup error.
- **`gc:N` (M0.2).** The allocation choke point counts GC-object allocations
  per carrier and sets the GC poll bit at every Nth. The collection runs at the
  next poll as a silent full collection (no callbacks, counts or statistics),
  with no skipping, so N sets the rate: under `gc:N` a dropped cycle is
  collected within about N allocations whatever the heap size. A collection
  costs the whole heap (about a millisecond), so `gc:1` costs a millisecond per
  allocation and only suits short programs.
- **Lanes.** `make check-kernel LANE=gc:N` picks the programs by the cost of
  the rate:
  - `gc:1` runs the curated `STRICT_PROGRAMS` in `tools/kernelcheck`: the
    programs that finish in under 40 s at that rate, which are the ones that
    exercise refcount and traverse paths without importing the compiler's
    Python (an uncaught exception does, and costs tens of millions of
    allocations).
  - `gc:100000` runs every program.
  - `tools/kernelcheck` also checks that a dropped cycle dies within N
    allocations under `gc:1`, `gc:10` and `gc:100`.
- **`deopt:N` (P0).** Guards are compiled to fail at a seeded phase on every
  Nth execution. Code compiled without the flag has no stress instructions.
- **`switch` and `sched`.** These are `-X lucifer_handoff` (DESIGN §5.8) with an
  optional `=SEED` choosing park-or-continue per safepoint. There is no second
  mechanism.
- **Stress never changes output.**

**Checks.**
- **K1** `check-kernel` passes under `-X lucifer_handoff=1` … `=8`, and
  `-X lucifer_stress=bogus` exits 2 with a message.
- **M0.2** `check-kernel` passes under `LANE=gc:1` and `LANE=gc:100000`, and difftest
  164/164 under `gc:100`.
- **P0** AWFY 14/14 and difftest 164/164 pass under `-X lucifer_jit=always
  -X lucifer_stress=deopt:7,gc:1000`.

### 2.8 Exception state only in the task

**Why.**
- 3.12 collapsed `curexc_{type,value,traceback}` into `current_exception`, and
  Cython reads that field directly (DESIGN §4.1).
- Cinder kept exception state in JIT frames and needed custom unwinding
  (unverified).
- With migration (DESIGN §5.3), exception state is task state by definition.

**Shape.**
- `current_exception` and the `exc_info` chain are the only stores. Every
  foreign C call is "may set an exception". The JIT checks a NULL return and
  then `current_exception`.
- At first, compiled code has no exception handlers. A raise in JIT code deopts
  at the raising instruction, and the interpreter's exception table unwinds.
- Kernel-hidden frames add no traceback entries.

**Checks.**
- **K1** `tests/kernel/exc_kernel.py` raises through kernel builtins (`sorted`,
  `dict.__getitem__`, `map`) with chaining, `sys.exception()` in generators,
  and `except*`. It matches the oracle.
- **M1.2** `tests/capi/exc_cross.c` covers:
  - C calling Python that raises;
  - Python catching an exception raised by C;
  - `PyErr_GetRaisedException`/`SetRaisedException` round trips;
  - a traceback listing only user frames.

  It matches the oracle.

### 2.9 `os.fork` with threads

**Why.**
- 3.12 began warning on fork with threads (gh-100228), because runtime locks
  held by other threads deadlock the child.
- cp314t stops the world around fork and clears other threads' BRC and
  mimalloc state in the child.
- Each lock added without an after-fork reset is a latent child deadlock.

Oracle PASS rows: `test_fork1` 3, `test_os` 320; 26 Lib/test files call
`os.fork()`.

**Shape.**
- Every runtime-internal lock is registered once in `_PyRuntime`'s lock
  registry: BRC buckets, the parking lot, interpreter and task lists, import
  locks, allocator hook state and the JIT queue.
- `PyOS_BeforeFork` stops the world and takes them all in registry order.
- `PyOS_AfterFork_Child` reinitializes them, abandons other carriers' heaps,
  drops their tasks and BRC entries, and restarts service threads (the JIT
  compiler).
- `pthread_atfork` is not used.

**Checks.**
- **Park/unpark rung** `tests/kernel/fork_threads.py` keeps 4 threads
  allocating and contending a `Lock`. It forks 20 times. Each child allocates,
  starts and joins a thread, then exits 0. It matches the oracle.
- **Park/unpark rung** `test_fork1` 3/3 passes.

### 2.10 Startup image readiness

**Why.**
- **CPython.** 3.11 added deepfreeze (bpo-45847), C-initialized code objects
  for startup speed. It had to be fixed for leaks and singletons (bpo-46481,
  bpo-46443) and for immortality.
- **The donor.** It needed snapshots to survive a second process under ASLR,
  "the fixed-VA code rule" (`docs/vm/e2e-DESIGN.md` §8).
- **Lucifer.** DESIGN §10 defers the image to a measurement. That is right
  only if init does not build state the image could not capture or rebuild.

**Shape.**
- Every object that kernel init creates is reachable from `_PyRuntime` or
  `PyInterpreterState`. This is enforced by the globals audit: no C static
  points into the heap.
- Init order does not depend on addresses or `id()` (no identity-keyed sets
  iterated during init).
- Frozen marshal data holds no `str` hashes: `ob_shash` is −1 on load. CPython
  marshal never stores hashes, and an image must reset them under a new
  `_Py_HashSecret`.
- JIT code is never part of init state.

**Checks.**
- **L1** `-X lucifer_heapdigest` prints a digest of the post-init object graph:
  types, shapes, values and edges, not addresses.
- **L1** Three runs with ASLR, `PYTHONHASHSEED=1` and `=2` print the same digest.

### 2.11 JIT IR/backend boundary (aarch64)

**Why.**
- **The donor.** Its optimizer emits x86-64 fasm, and DESIGN §6.2 plans a C
  x86-64 encoder.
- **CPython.** Its copy-and-patch JIT supported aarch64 from 3.13 because the
  stencils come from LLVM per target.
- **Cinder.** It stayed x86-64 (unverified).
- **The cost.** Arch assumptions (flags, two-address forms, `%fs:0`, red zone)
  leak into IR passes unless a boundary exists before the first pass.
- **cp314t.** `_Py_ThreadId()` is already arch-neutral (`tpidr_el0` on aarch64).

**Shape.**
- `runtime/jit/{ssa*,regalloc}.c` include only `jit/target.h`, which provides:
  - register classes and the calling convention;
  - `emit_*` for the lowered ops;
  - the three-instruction poll (DESIGN §5.6);
  - call, deopt stub, frame descriptor and code publication.
- `runtime/jit/x86_64/` implements it. Assembly lives under
  `runtime/asm/<arch>/`.
- The interpreter and runtime contain no arch `#ifdef` outside `_Py_ThreadId`
  and mimalloc.

**Checks.**
- **L2** `make ARCH=aarch64 CC=aarch64-linux-gnu-gcc` builds `bin/lucifer`, and
  `check-kernel` passes under `qemu-aarch64`, interpreter only. This needs
  owner-approved cross and qemu packages.
- **P0** The same build compiles `runtime/jit` with an aarch64 `target.h` stub,
  and `-X lucifer_jit=always` there reports "no backend" once and runs
  interpreted.

### 2.12 Observability

**Why.**
- **perf.** `-X perf` trampolines arrived in 3.12 (gh-96143), and `-X perf_jit`
  jitdump in 3.13 for unwinding without frame pointers (gh-118518).
- **External inspection.** 3.13 added `_Py_DebugOffsets` in `_PyRuntime`
  (cookie `xdebugpy`, ELF section `.PyRuntime`). py-spy-style readers,
  `python -m asyncio ps` and PEP 768 depend on it.
- **The donor.** It ported `perf_map.inc` from the start.
- **Unwinding.** Native unwinding through JIT frames is impossible to add once
  frame layouts omit a frame-pointer chain.

Oracle PASS rows: `test_perf_profiler` 12, `test_perfmaps` 1,
`test_faulthandler` 44, `test_external_inspection` 24. regrtest relies on
faulthandler (§2.2).

**Shape.**
- The runtime builds with `-fno-omit-frame-pointer` (K0.1).
- JIT frames keep the frame-pointer chain and are registered in the perf map
  and jitdump at the one publication point (DESIGN §6.2.7).
- `-X perf` uses the `PyUnstable_PerfTrampoline_*` exports (K0 stubs).
- `_PyRuntime` starts with a `_Py_DebugOffsets` that describes Lucifer's task
  and frame layouts (L3).
- faulthandler is a native module that walks §2.2's chain without allocating.

**Checks.**
- **M1.1** libtest `test_faulthandler` reaches ≥ 90% of the oracle's 44.
- **M1.1** A busy loop under `faulthandler.dump_traceback_later(0.2)` prints the
  loop's frame.
- **P0** `test_perf_profiler`/`test_perfmaps` at oracle parity with
  `-X lucifer_jit=always`.
- **P0** `perf record -g` on AWFY `richards` resolves ≥ 95% of samples inside
  JIT code to `py::` symbols.
- **L3** `test_external_inspection` at oracle parity; `sys.remote_exec` runs a
  script.

### 2.13 Finalization and embedding

**Why.**
- PEP 587 (3.8) retrofitted `PyConfig` after years of scattered `Py_*Flag`
  globals.
- Finalization order bugs kept arriving:
  - daemon threads touching freed state;
  - 3.14 now raises `PythonFinalizationError` when joining daemon threads at
    shutdown (gh-130402);
  - and hangs re-attaching daemon threads.
- `test_embed` is SKIP in the oracle, so embedding is not on the bar. The
  archive shape costs one Makefile rule now, and a relink later.

**Shape.**
- `main.c` already calls `Py_BytesMain`. That function is PyConfig, then
  `Py_InitializeFromConfig`, then `Py_RunMain`, and nothing else starts the
  runtime.
- `Py_FinalizeEx` follows `Python/pylifecycle.c` order:
  1. `threading._shutdown`;
  2. atexit (per interpreter);
  3. daemon tasks park forever at their next attach;
  4. set `_PyRuntime` finalizing;
  5. flush stdio;
  6. GC;
  7. module teardown;
  8. subinterpreters ended.
- `bin/lucifer` links `main.o` with `build/liblucifer.a` (K0.1).

**Checks.**
- **M0.0** `tests/kernel/shutdown.py` covers atexit order and exceptions,
  `__del__` at exit, `sys.exit` codes, and an exception in an atexit callback.
  It matches the oracle.
- **Park/unpark rung** A daemon thread is still running at exit, a non-daemon
  thread is joined, and the exit code matches.
- **M0.0** `tests/embed/run.c`, linked to `build/liblucifer.a`, runs
  `PyRun_SimpleString` twice across `Py_FinalizeEx`/`Py_Initialize`. Its output
  matches `bin/lucifer`.

### 2.14 Weakrefs, finalizers and resurrection under free threading

**Why.**
- The cp314t semantics were reworked for free threading:
  - `_PyObject_ResurrectStart/End`;
  - striped weakref locks;
  - the `_PyGC_BITS_FINALIZED` flag in `ob_gc_bits`.
- Built-in types written in Python dealloc through generic paths, so a second
  dealloc path would split behaviour.
- The work is a port; the risk lies in path count.

Oracle PASS rows: `test_weakref` 139, `test_finalization` 18, `test_gc` 53.

**Shape.**
- One dealloc sequence: `tp_finalize` via `PyObject_CallFinalizerFromDealloc`,
  then weakref clear, then `tp_dealloc`.
- It is used by refcount-zero, BRC merge and the cycle GC.
- `gc.freeze` is the cp314t frozen bit, a flag check, and needs no generation
  list.

**Checks.**
- **M0.2** `tests/kernel/resurrect.py` covers:
  - `__del__` resurrecting once (finalizer not re-run);
  - weakref callbacks ordered against `__del__`;
  - a `WeakValueDictionary` under `gc.collect`.

  It matches the oracle.
- **Park/unpark rung** The ftcorpus weakref and finalizer cases pass serially
  and in parallel.

### 2.15 Imports, `.pyc`, zipimport and bootstrap

**Why.** Lucifer runs CPython's own `importlib._bootstrap[_external]` and
`zipimport.py`, frozen (DESIGN §3.3). The retrofit risk is a private import
path that bypasses `_imp`: PyPy's own importer had to chase CPython for years
(unverified). The NumPy subinterpreter message comes from `_imp`'s handling of
`Py_mod_multiple_interpreters`.

**Shape.**
- `_imp` provides CPython's set: `create_builtin`, `exec_builtin`,
  `create_dynamic`, `exec_dynamic`, `is_frozen`, `find_frozen`,
  `get_frozen_object`, `_fix_co_filename`, `source_hash`,
  `check_hash_based_pycs`, `extension_suffixes` and the import lock.
- Multi-phase init reads every `PyModuleDef_Slot` (`Py_mod_create`,
  `Py_mod_exec`, `Py_mod_multiple_interpreters`, `Py_mod_gil`) and stores the
  per-interpreter state.

**Checks.**
- **M0.0** `tests/kernel/imports.py` covers:
  - an mtime-bumped source invalidating its `.pyc`;
  - `py_compile` with `PycInvalidationMode.CHECKED_HASH`;
  - an edit plus `importlib.invalidate_caches()`;
  - an import from a zip on `sys.path`;
  - `importlib.reload`.

  It matches the oracle.
- **M1.2** `tests/capi` checks a module with `Py_mod_multiple_interpreters`
  and `Py_mod_gil` slots: the slots are read, and an unknown slot fails as in
  the oracle.

### 2.16 Hash secret and seed

**Why.**
- Hash randomization was retrofitted under hashDoS in 3.3 and became SipHash
  with PEP 456 (3.4).
- Follow-ups fixed flags and secondary seeds: bpo-30416, CVE-2018-14647 in
  `_elementtree`.
- Every harness pins `PYTHONHASHSEED=0`, which would hide a missing secret path.

The oracle reports `hash_randomization=1` and `siphash13`.
`PYTHONHASHSEED=12345` gives `hash("abc") == -4296600278565060869` and set
order `['c','d','b','a']` for `{"a","b","c","d"}`.

**Shape.**
- `_Py_HashSecret` (exported, 24 B, process-wide as in CPython) is filled from
  PyConfig (`PYTHONHASHSEED`, or `getrandom`) before any `str` is hashed.
- The kernel never hashes at freeze time (§2.10).

**Checks.**
- **K1** `tests/kernel/hash_seed.py` is run with `PYTHONHASHSEED=12345` and
  `=0` and matches the oracle at each seed.
- **K1** Without a seed, two runs print different `hash("abc")`, and
  `sys.flags.hash_randomization == 1`.

### 2.17 Audit hooks (PEP 578)

**Why.**
- Audit events sit in many native paths. PyPy 7.3.20 (2025) was still adding
  the `sys._getframe` audit event (`pypy/doc/release-v7.3.20.rst:159`).
- Oracle PASS rows: `test_audit` 26.

**Shape.**
- One `PySys_Audit` primitive, callable from kernel Python.
- C hooks (`PySys_AddAuditHook`, pre-init) live in `_PyRuntime`, and
  `sys.addaudithook` hooks live in `PyInterpreterState`.
- Raising sites call it like CPython's.

**Checks.**
- **M0.0** `tests/kernel/audit.py` hooks and prints events for `compile`,
  `exec`, `open`, `import` and `sys._getframe`. It matches the oracle.

### 2.18 PEP 523 frame-eval hook

**Why.**
- `_PyInterpreterState_SetEvalFrameFunc` is exported (K0 stub).
- Debuggers (pydevd before 3.12) and TorchDynamo install it (unverified usage
  count). They also read `_PyInterpreterFrame` internals, which Lucifer cannot
  honour.
- Supporting the hook later would mean finding every inlined call site.

**Shape.**
- Every Python frame entry goes through one `lu_enter_frame`.
- A non-NULL `interp->eval_frame` routes entry through the hook and disables JIT
  entry for that interpreter.

**Check.**
- **M1.3** `tests/capi/eval_hook.c` installs a counting hook that delegates to
  `_PyEval_EvalFrameDefault`. Calling a Python function three times from a hot
  loop counts 3 entries, with or without the JIT. After the hook is removed,
  the count stops.

## 3. Conflicts in landed K0 (`runtime/`, merged at 9cde773)

| # | Where | Conflict | Needed shape | Severity |
|---|---|---|---|---|
| C1 | `runtime/object/brc.c` | The bucket table is a process-global `static struct bucket table[257]`. The owner record is a second `_Thread_local self`, created on a thread's first allocation and removed by a pthread-key destructor. | Buckets in `PyInterpreterState` (cp314t `interp->brc`). The record is a field of the per-(carrier, interpreter) entry reached through the one carrier TLS word (DESIGN §5.3), created and retired with the carrier. The owner drains at polls (`_PY_EVAL_EXPLICIT_MERGE_BIT`). Registered for after-fork reset (§2.9). The race review ticket covers the same file. | High: K1's carrier record and GC build on it. |
| C2 | `runtime/object/refcount.c` `_Py_NewReference` | Calls `lu_brc_register_thread()` on every object birth, coupling allocation to BRC lifecycle with a branch on the hottest path. There is no reftracer hook in `_Py_NewReference`/`_Py_Dealloc`. | Registration belongs to carrier creation. Both functions test the reftracer pointer. | Medium |
| C3 | `runtime/object/alloc.c` | `PyMem_*` and `PyObject_*` call `mi_malloc` directly. All domains share mimalloc's default heap. `PyMem_SetAllocator`/`GetAllocator` are aborting stubs. There is no GC-object entry. | Domain tables in `_PyRuntime`. Default contexts pick the (carrier, interpreter) heap by id (`MEM`/`OBJECT`/`GC`/`GC_PRE`). `lu_gc_alloc`. Real Set/GetAllocator (§2.6). The cycle GC (DESIGN §4.3) walks GC heaps, so objects allocated into the default heap now would be invisible to it. | High: every later allocation site copies whatever K0 exposes. |
| C4 | `runtime/types/static.c` | `_PyRuntime` is an opaque `unsigned char[365624]`, and runtime-wide state is otherwise free to land in new file-scope statics. | `_PyRuntime` becomes the runtime record (§2.1) with the `_Py_DebugOffsets` head reserved, and `make check-globals` enforces it. | Low now, compounding |
| C5 | `docs/DESIGN.md` §4.1 "the kernel fills in their dicts" | The kernel writes static types' dicts into the shared struct. | The dict goes to per-interpreter `managed_static_type_state`, with `tp_dict` NULL as in the oracle (§2.1). | Medium: a one-line design change now, a kernel-wide change later. |

No other K0 code contradicts a shape. The stubs, symbol visibility, immortal
statics and legacy flag globals match CPython, which keeps those flags global
too.

## 4. New rungs and ticket changes

| Rung | Content | Depends on | Unblocks |
|---|---|---|---|
| K0.1 | C1–C4 fixes, allocator domains, `make check-globals`, `-fno-omit-frame-pointer`, `liblucifer.a`, lock registry | K0 | K1 |
| K1.1 | `-X lucifer_subinterp` lane over `check-kernel` | K1 | M1.6a |
| M0.3 | PEP 669 instrumentation in the interpreter; legacy tracing over it | M0.1 | P0 |
| M1.1a | `_tracemalloc` over the allocator domains and the frame walker | K0.1, M1.1 | M1.7 |
| M1.6a | `_interpreters`, `concurrent.interpreters`, `Py_mod_multiple_interpreters` | K1.1, M1.2, M1.1 | M1.7 |
| L1 | `-X lucifer_heapdigest` determinism | M0.0 | §10 image experiment |
| L2 | aarch64 interpreter lane under qemu | K1 | — |
| L3 | `_Py_DebugOffsets`, `sys.remote_exec`, external inspection | M1.1 | — |

Acceptance lines were added to existing rungs:
- **K1:** frames, invalidation, exceptions through the kernel, hash seed, poll
  reasons and the stress parser.
- **M0.0:** signals, shutdown, embedding, imports and audit.
- **M0.2:** `gc` stress and resurrection.
- **M1.1:** faulthandler.
- **M1.2:** exceptions across C and `Py_mod_*` slots.
- **M1.3:** watchers and async exceptions.
- **P0:** deopt stress, tracing and frames under the JIT, perf and frame
  pointers, and the aarch64 build.
- **Park/unpark rung:** fork with threads, daemon shutdown and ftcorpus
  weakrefs.

M1.7 now also depends on M1.1a and M1.6a, because two oracle-PASS NumPy cases
need them.
