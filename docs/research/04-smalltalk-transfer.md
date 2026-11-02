# Smalltalk transfer: mechanisms, compatibility boundaries, and measured build rate

Research date: 2026-11-02. Recommendation: build Lucifer in its own repository,
copy selected implementation pieces with provenance, and adapt their contracts.
Do not begin with a shared object model, collector, scheduler, or compiler core.
The strongest transferable asset is the agreement between execution, root
enumeration, deoptimization, publication, and observation; Python's C-API changes
the representations at each of those boundaries. This is a design inference from
the subsystem inventory below, not a demonstrated Python implementation.

## Evidence and counting conventions

Lucifer's `README.md` fixes full non-limited C-API support, an unmodified NumPy
build passing NumPy's own suite, no GIL, and built-ins/stdlib mostly implemented
in Python over native primitives. None is relaxed here. The three milestones in
§5 are proposed research checkpoints, since `README.md` does not name three
implementation milestones; the NumPy/full-C-API bar is in the first one.

`ST:` means a repository-relative tracked path in the Smalltalk VM, read as a
source blob at `origin/master`; inventories came from tree listings at that
reference. History came from read-only commit-log analysis. The final census began and ended at
**`359e333619075e35da1d2c3a8433609f5fc15591`**, committed
**2026-11-02 01:45:02 UTC**. `origin/master` advanced during research; earlier
counts were refreshed. Paths/line numbers below refer to this final revision
unless a historical commit is specified. The revision above and the
repository-relative source paths below identify the evidence.

Read first: Lucifer `AGENTS.md`, `README.md`; then `ST:AGENTS.md`,
`docs/04-SYSTEM-MAP.md`, `docs/05-INVARIANTS.md`, `docs/ROADMAP.md`,
`docs/CONVENTIONS.md`; followed by runtime headers and relevant `docs/vm/`
records. The map has 1,282 lines and the invariant register 3,030. They are useful
navigation, but contain historical descriptions: e.g. the map's scheduler table
still describes older tiers, while `vm/runqueue.inc` implements lock-free work
stealing. `docs/vm/value-layout-DESIGN.md` says “Design, not built”, but its L1
subsequently landed. Source and dated landing evidence take precedence.

Sizes are **physical source lines, including comments and blanks**, not logical
instructions or net effort. Current file counts use `len(blob.splitlines())`.
Historical counts sum additions minus deletions along the **first-parent** chain,
including each merge's diff against its first parent, with rename detection off.
Thus a moved file subtracts at its old path and adds at its new path; branch work
is not counted again for every merge. Current top-level VM and total assembly
counts reconcile with the history reconstruction. Subsystem sizes overlap and
must not be summed; a multi-purpose file is charged in full and labeled as such.
No donor/vendor code was built, installed, or executed; no benchmarks were run.

Other pinned local sources: `vendor/cpython` is CPython 3.14.0,
`ebf955df7a89ed0c7968f79faec1de49f61ed7cb`; `vendor/numpy` is
`df9913aa4ea20c33728fbc434acc66c3285f12a2`. Python claims below refer to those
sources, with current official documentation used as an additional check. Online
3.14 documentation can describe later maintenance releases than the local 3.14.0
checkout.

## 1. Transfer inventory

“Copy” means a largely language-neutral implementation can be brought across
after path/configuration changes and re-verification. “Adapt” retains the
mechanism while replacing semantic interfaces. “Redesign” means its current
contract conflicts with Python compatibility. These are recommendations, not
claims that copied code is already verified in Lucifer.

| Subsystem | Counted donor source and size | Verdict | What transfers; what changes for Python |
|---|---|---|---|
| Assembler/toolchain | `ST:Makefile` 522; `lib/sys.inc` 387; `vm/zerofill.inc` 60; `tools/split-code-data.py` 364; `tools/check-entries.py` 105; `tools/mkpie.sh` 453; `tools/link-vm-pie.sh` 53: **1,944 lines** | **Adapt** | Keep fasm 1.x, explicit native contracts, storage/entry validation, and cheap builds if Linux/x86-64 remains the initial target. The freestanding static ELF/zero-libc/no-linking policy in `docs/CONVENTIONS.md` cannot simply become Lucifer policy: NumPy and its shared-library dependencies need a compatible loader, symbol visibility, platform ABI, and foreign runtime integration. The actual Makefile uses `fasm -m262144`; the conventions' `-m32768` prescription is stale. No claim that this toolchain alone supplies dynamic extension loading. |
| Object memory and headers | `ST:vm/vm.inc` **4,183**; `native_storage.inc` **171**; `identity_hash.inc` **1,771**; `region_index.inc` **398**: **6,523**, sharing region code with GC | **Redesign representation; adapt allocation machinery** | Keep checked size arithmetic, region topology, side metadata, explicit ownership. Current objects have an 8-byte ClassHot pointer followed by qword fields; nil is zero and small integers/Floats/Characters are tagged (`vm.inc:61–109`). Full-API `PyObject*`, type headers, extension subtype layouts, identity, refcounts, weakrefs and C-visible buffers require a different public representation. Fixed heap addresses do not make a moving young object safe for C. |
| GC and write barriers | `ST:vm/{gc,gc_accounting,gc_census,gc_parallel,promo_workspace,region_index,remset_dir}.inc`: **7,349 / 7 files** | **Redesign C-visible lifetime policy; adapt collector algorithms** | Worker-private copying nurseries, an all-nursery stop, regional mature collection, exact remembered pairs and one root visitor are useful mechanisms (`gc.inc` header; `INV-GC-1/2/3/5`). Raw same-region stores need no generational record; cross-region references do. C macros can write fields without calling that barrier, extensions can retain pointers, and refcount-to-zero can invoke Python. Decide tracing/refcount cooperation, nonmoving exposed objects, native roots and uninstrumented stores before copying the collector. Whole-collector drop-in reuse is unverified. |
| Template tier | `ST:vm/tc.inc` **5,311** + `tc_special.inc` **1,037** = **6,348** | **Adapt** | Keep copying position-independent per-op templates, operand/branch patches, bytecode↔native maps, artifact lifetimes and cold code publication. Replace Smalltalk opcodes, sends, primitive exits and frame conventions with Python operations and exception states. Its templates depend directly on `FH_*`, `P_*`, `gc_*` and `sl_*`; they are not an independent Python backend (`tc.inc:1–62`). |
| SSA optimizing tier | `ST:vm/optimizer*.inc`: **56,465 / 51 files**; the `optimizer_ssa*.inc` subset is **28,338 / 14 files** | **Adapt IR/backend; redesign semantic lowering** | CFG/SSA, explicit effects, inlining, type/range facts, representation selection, cold guards, root maps, background builds and epoch-checked publication transfer. Python operations may call descriptors, numeric reflected methods, iterators or arbitrary C callbacks. Those effects and exception edges must be explicit. A sampled type is a hint; a guard and valid lifetime make it a proof (`optimizer_ssa.inc`, `optimizer_ssa_opt.inc`, `optimizer_background.inc`; `docs/vm/optimizer-backend-DESIGN-a.md` §§2–3). |
| Register allocator | `ST:vm/optimizer_ssa_allocate.inc` **1,579**; related older planners `optimizer_registers_plan.inc` **954**, `optimizer_registers_emit.inc` **294** | **Adapt** | Current allocator uses per-block fragments of complete State liveness, GPR/XMM banks, local linear scans, spill homes and edge reconciliation; its comments at lines 1–4 and 636 onward describe the actual mechanism. Keep those algorithms, replace pinned registers, Python call clobbers, native-call ABI and root/reconstruction recipes. Do not copy the superseded slot planners merely because they remain present. |
| Deoptimization/reconstruction | `ST:vm/optimizer_inline_runtime.inc` 450 + `optimizer_calls.inc` 2,745 + `optimizer_layout.inc` 949 + `optimizer_cold.inc` 208 + `optimizer_ssa_auth.inc` 1,513: **5,865**, broad shared files; emission also lives inside the 10,239-line `optimizer_ssa_emit.inc` | **Adapt protocol; redesign Python state recipes** | Preserve exact before/after operation states, virtual activation identity, immutable descriptors and reconstruction before any observer. `opt_ssa_deopt` publishes the node's BEFORE state at its unexecuted source opcode (`optimizer_ssa_emit.inc:2198–2225`); `INV-EXE-1` includes delayed root/object programs. Python recipes must reproduce cells, exception state, suspended generators, aliases and C-visible identities without replaying completed side effects. The canonical execution floor is the recovery authority; older docs disagree about whether a particular exit resumes templates or the interpreter. |
| Type feedback and inline caches | `ST:vm/source_send_cache.inc` **1,609**, `lookup_cache.inc` **111**, `optimizer_feedback.inc` **1,093**, `persist_feedback.inc` **418**: **3,231** | **Adapt** | Keep caller+opcode site identity, stable weak site records, class guards, dependency invalidation, advisory profiling and exit-driven recompilation. Python needs guards for type/MRO changes, descriptor behavior, instance dictionaries/slots, globals and C slot changes, not just receiver ClassHot plus selector. Persisted feedback is currently **write-only**; its header explicitly says current execution supplies heat after restoration experiments regressed. “No re-warming” is not an implemented transferable result. |
| Flat layouts and storage strategies | `ST:vm/field_kinds.inc` **3,697**, `storage_strategies.inc` **537**, `migrate.inc` **1,347**, `source_reshape.inc` **2,569**: **8,150** | **Adapt mechanisms; redesign eligibility** | Field-kind/layout versions, reference maps, whole-constructor strategy selection and widening on incompatible stores are valuable. Current `storage_strategies.inc:1–18` implements Boolean/integer physical Array variants while preserving semantic class and holder identity. This is not proof of general inline composite values. Python `__dict__`, slots, extension offsets, element identity and external buffer aliases constrain specialization. A `list` of existing Python objects cannot discard their identity merely because their values have a homogeneous type. |
| Float unboxing | `ST:vm/source_float.inc` **656**, `optimizer_ssa_float.inc` **2,400**, `field_kinds.inc` **3,697**: **6,753**, overlapping layout work | **Adapt arithmetic/codecs; redesign identity and nil policy** | Raw binary64 fields, XMM values and layout-driven GC skipping transfer. The donor's new `INV-REP-1` makes Float identity/hash depend on bits, permits equal-bit boxes to be identical, and reserves a NaN family for nil. Python/C-created Float objects have observable object identities; Python NaNs must not silently become `None`. Preserve escaping identities and aliases, and use separate presence metadata or a proved escape/generalization path. Unboxing itself remains available; adopting the donor's new Float semantics is not justified. |
| Scheduler and workers | `ST:vm/{sched,worker_state,pcb,runqueue,semaphore,sem_index,sem_pool,wait,fairness,process_control,process_interrupt}.inc`: **13,575 / 11 files** | **Adapt queues/ownership; redesign Python thread mapping** | Keep M:N cheap execution contexts, segmented exact-sized frames, work stealing, run-next slots, futex parking, one eligibility authority, wait incarnation IDs and native lifetime pins. `runqueue.inc` is lock-free dispatch, not the older global-queue design; semaphores have striped indices (`sem_index.inc`, system map §1). Python `threading`, OS TLS, `PyThreadState`, signals, recursive callbacks and blocking C calls need distinct execution rules. Start with native-thread affinity while a C stack is active; arbitrary migration across C calls is unverified. |
| Safepoints | Embedded in the **7,807-line** `ST:vm/sched.inc` and **5,311-line** `tc.inc`, plus SSA emission. A bounded epoch/poll sample is **479 lines**: `sched.inc:3975–4416` (442), `tc.inc:3146–3182` (37); this is not the complete safepoint implementation | **Adapt cooperative protocol; redesign foreign-thread participation** | Keep composable GC/snapshot/maintenance/debug stop reasons, generation receipts, exact-once acknowledgments and complete continuations before ACK (`sc_poll_core`, `sc_epoch_request`, `sc_quiesced`; `INV-SCH-7`, `INV-EXE-1`). A native extension may run without JIT backedges or retain pointers while detached. Define attached/detached C states, roots and stop admission; waiting for every extension to reach a Smalltalk poll can deadlock collection. Donor instruction slopes are evidence for that donor, not Python overhead predictions. |
| io_uring networking and native I/O | `ST:vm/socket*.inc` **6,207 / 12 files** + `driver_io.inc` **687** + `driver_native.inc` **309** = **7,203** | **Adapt** | Exact request/incarnation ownership, copied native buffers, cancellation/CQE retirement, deadlines, partial I/O and resource transfer are reusable. Current transport is hybrid: worker-side send/receive, epoll parking for untimed reads, io_uring for other/timed operations, helper-backed regular-file writes (`socket_poll.inc`, `socket_deadline.inc`, `driver_native.inc`; system map §1). Adapt socket/file/error APIs and integrate `asyncio`; replacing Python's event-loop behavior by Smalltalk Process parking is not automatically compatible. |
| Snapshots and persistent store | `ST:vm/snap*.inc` plus `live_reclaim.inc`, `vm_snap_service.inc`, `wait_batch.inc`: **13,790 / 14 files** | **Adapt store protocol; redesign admissible Python state** | Keep checked wire formats, one manifest commit point, fsync ordering, owned capture/finalizer lifetimes, logical-frame capture, lazy validation and kill-matrix testing. `snap_delta.inc` already adds sparse page-image generations without FICLONE; this is not solely a btrfs idea. Python C heaps, BLAS/library state, foreign threads, open sockets and native callbacks cannot be restored by copying managed bytes. First define what can be snapshotted and how foreign resources detach/reopen; whole-process extension snapshots remain unverified. This feature is not required by Lucifer's current README. |
| perf map/jitdump | `ST:vm/perf_map.inc` **531** | **Copy writer; adapt symbols/lifetimes** | Linux perf-map and jitdump record emission is mostly language-neutral. Replace environment/name conventions, register Python code/line provenance and keep output publication synchronized with artifact lifetime. Retain the ability to distinguish template, optimized and native-runtime time (`perf_map.inc:1–19`; `docs/CONVENTIONS.md`, Benchmarks). Source attribution matters more than a single combined JIT percentage. |
| Shared build and benchmark locks | **463 lines**, external shell implementation; **0 tracked files named for that service in the Smalltalk VM** | **Copy usage; share existing external tool** | The shell implementation uses flock-backed shared build/test slots, dedicated core-pair leases and exclusive timed-benchmark admission, with lifecycle cleanup. Its header/usage and `ST:AGENTS.md` define the separation. It is infrastructure, not a VM dependency; use the same host-wide service rather than independent locks in two repos. This previously recorded working-tree count is not pinned to the Smalltalk VM revision; independent reproduction from the public sources is **unverified**. No service command was executed during this research. |
| Test harness | `ST:lib/test.inc` **197**, `print.inc` **86**; `tools/lib/scenario.sh` **125**, `tools/e2e.sh` **417**, `tools/scratch-root.sh` **14**, `tools/test-run.sh` **50**; `vm/wstat.asm` **102**: **991**. Assembly fixtures: **41,508 / 110 files** under `vm/tests/` | **Copy lifecycle/oracles; adapt tests** | Preserve bounded real-executable scenarios, exact result bytes, exit-versus-signal distinction, isolated scratch, failure retention, anti-vacuity and owned child teardown. Adapt payloads to Python. `AGENTS.md`/`CONVENTIONS.md` now favor observable behavior; old tests of fixed counters/instruction totals still exist and are not the standard to reproduce. Test harness size is separate from fixture population. |
| Conformance suites | `ST:tools/test-conformance.sh` **283** + `tools/lib/conformance-tables.sh` **317** = **600**; **127** conformance corpus files, **1,299 non-comment rows / 37 `.cases` files**. Camp ANSI donor `tests/ansi-st/{Ansi,AnsiDB,AnsiInit}.st`: **24,603**; runner `tools/ansi-st.sh` **112** | **Adapt runner; replace language oracles** | Keep interpreter/template/optimizer and restart/configuration matrices, explicit deviation records and a donor-source scoreboard. Smalltalk/Lisp pass counts establish no Python compatibility. Use CPython tests, differential Python programs and NumPy's complete applicable suite with native extensions. Reconcile baseline/platform skips explicitly; do not manufacture a pass by adding Lucifer-specific skips or marking known failures expected. Counts here describe available tests, not current pass totals. |
| AWFY and all-runtimes harness | `ST:benchmarks/awfy/*.st`: **5,240 / 16 files**, including Core/Harness and 14 workloads; `tools/bench-awfy.sh` **50**. Eight `.py`/`.sh` files under `tools/perf/all-runtimes/`: **943**; `reference.json` **3,171** is data, excluded | **Copy measurement method; adapt runtime inventory** | Keep verified inner work, raw logs, pinned versions, alternating runtime order, CPU/sibling occupancy, warmup/steady classification and geometric means. Replace hardcoded host/runtime paths and include CPython, PyPy and free-threaded CPython as first-class references. AWFY alone lacks Python imports, descriptors, C-API and real package workloads: extend with the local `vendor/pyperformance` corpus and application/package cases, as Lucifer README requires. |
| HTTP tiers | `ST:benchmarks/http-tiers/{HttpTiers.st,pharo-tiers.st,go-tiers.go,verify.sh}`: **152+128+87+186 = 553**; `tools/perf/http-window.sh` **94**, `tools/lib/http-load.sh` **53**, `tools/bench-theses-scaling.sh` **58**: another **205** | **Adapt** | Keep equivalent plaintext/JSON/HTML bodies, pipelining as its own row, correctness before timing, separate load-generator/server CPUs, CPU/request and scaling. Fortunes here uses an in-memory table, not a database (`benchmarks/http-tiers/README.md`); it is TechEmpower-style, not a full TFB result. Implement handlers in ordinary Python over its real server/async APIs; no endpoint-specific native shortcut. |
| Gate and performance regression process | `ST:tools/perf/gate.sh` **481**, `quick.sh` **185**, `icount.sh` **390**, `status.sh` **640**: **1,696**; `paired.awk` **67**, `verdict.awk` **19**, `corpus.awk` **48**: **134** supporting lines | **Adapt policy; copy comparison discipline** | Keep candidate/base binaries with their own source/seed provenance, paired controls, invalid/inconclusive outcomes, correctness before performance and stored raw evidence. The older gate includes timed HTTP/corpus/AWFY; current `AGENTS.md` instead requires build+smoke+30-second quick checks before landing, then full tests/instruction counts after landing, with timed trains periodic. Adopt one explicit Lucifer policy; do not accidentally reinstate obsolete per-change timed gates. |

The missing frontend is consequential: `ST:vm/source_language.inc` alone is
**6,837 lines**, with `source_compile.inc` another **910**, and surrounding
source-publication/reflection files beyond that. Smalltalk lexical scope,
message precedence, cascades, class declarations and non-local returns are not a
Python parser/compiler. Reuse publication and immutable-code ownership ideas;
build Python grammar, symbol analysis, code/line/exception metadata, import and
module initialization around Python semantics. Evidence: these two source
headers/bodies and `ST:docs/04-SYSTEM-MAP.md:421–478`; Python comparison points
are `vendor/cpython/Grammar/python.gram`, `Python/symtable.c`, `Python/compile.c`
and `Python/codegen.c`.

## 2. The transferable contract, and where it stops

The donor's useful spine is one semantic state with several consumers:

| Contract to retain | Donor authority | Python addition required |
|---|---|---|
| One root enumeration authority | `ST:vm/sched.inc:sc_roots_visit`; `INV-GC-1` | Native extension roots, refcount-owned objects, borrowed-reference lifetimes, buffers and foreign thread states must participate or remain nonmoving. |
| Reconstruct before observing | `ST:docs/05-INVARIANTS.md:1639–1662`, `INV-EXE-1` | Python frames/tracebacks, debugger/trace hooks and C re-entry are observers; completed side effects cannot be replayed. |
| Validity revoked before new optimized execution | `ST:vm/optimizer_dependencies.inc`, `optimizer_live.inc`, `source_send_cache.inc`; `INV-REP-4` | Type dictionaries, bases/MRO, descriptors, instance dictionaries and C type slots introduce more invalidation sources. |
| Events post; one authority admits | `ST:vm/sched.inc:sc_eligible`, `wait.inc`; `INV-SCH-1` | Native OS-thread identity and Python thread/task identity cannot be silently conflated. |
| Durability has one commit authority | `ST:vm/snap.inc`; `INV-SNAP`, system map §3 | Only a specified Python/extension state domain can be durable; native pointers are not a portable semantic snapshot. |

Two boundaries deserve decisions before transfer implementation:

**C-visible representation.** CPython 3.14.0's `Include/object.h:110–166`
defines different regular/free-threaded object headers. From the declared x86-64
fields, the regular header is 16 bytes and the free-threaded header 32 bytes
(source-layout calculation, not a compiled `sizeof` measurement); the latter
includes thread ownership, mutex/GC bits and local/shared counts. The donor's
8-byte header is a different ABI. `Include/cpython/{listobject,longintrepr,
unicodeobject,object}.h` exposes additional layouts/inlines. A table of exported
function names cannot repair incompatible compiled field offsets.

**No-GIL safety.** Free-threaded CPython gives containers internal synchronization,
but direct struct access, unchecked macros and some borrowed references still
require caller discipline; `PyDict_Next` is a documented example. NumPy's local
`numpy/_core/src/multiarray/multiarraymodule.c:5793–5796` declares
`Py_MOD_GIL_NOT_USED`. These establish a concrete no-GIL extension target, not a
proof that every arbitrary legacy extension becomes race-safe. Lucifer cannot
use CPython's automatic GIL-enable fallback to satisfy the owner's no-GIL rule.
See [extension guidance](https://docs.python.org/3.14/howto/free-threading-extensions.html)
and local `vendor/cpython/Doc/howto/free-threading-extensions.rst`.

Candidate memory policies, all **unverified for Lucifer**:

| Policy | Consequence under the fixed requirements | Assessment |
|---|---|---|
| Canonical C-compatible, nonmoving exposed objects; JIT-private unboxed values with explicit materialization | Simplest pointer/alias story; must integrate refcounts, cycles, weakrefs, finalizers and container locking. Regions/side metadata and JIT allocation elimination can still help. | Recommended first prototype, not a performance verdict. |
| Separate managed objects plus stable C-facing mirrors/handles | Full-API macros demand real structs, not opaque handles. Both views must share identity and immediately coherent mutations, including writes with no callback into Lucifer. | Consider only after demonstrating coherence at those writes; a handle table alone is insufficient. |
| Move exposed objects using the donor nursery unchanged | Existing C pointers/interior buffers do not change when the GC repairs JIT slots. | Reject this unchanged transfer; moving private, provably unexposed allocations is a separate option. |

These inferences follow from `ST:vm/gc.inc:8–28`, `INV-GC-1/2/5`, CPython's
headers, and NumPy's `numpy/_core/include/numpy/ndarraytypes.h:787–856,1605–1615`
(C-layout objects and direct array-data access). They do not require rewriting
built-ins in C: Python implementations can operate on ABI-backed storage through
native allocation/locking/reference/buffer primitives, with native C-API entry
stubs invoking compiled Python. The completeness, bootstrap cost and primitive
count of that design are **unverified**.

## 3. Python-specific gaps

Several requested gaps have partial Smalltalk counterparts. Calling them absent
would erase useful evidence: the donor has arbitrary-size arithmetic, exceptions,
closures and Contexts. What it lacks is the **Python contract**. CPython file
lengths below measure semantic reference surface, not a prediction of Lucifer
code size or an instruction to port those C implementations wholesale.

| Python requirement | Actual donor counterpart | Python work not supplied by it; source evidence |
|---|---|---|
| Arbitrary-precision `int` as the single ordinary integer type | Tagged SmallInteger plus LargeInteger; `ST:vm/source_large_integer.inc` **959** lines, base-10^9 tagged limbs, ordinary-source fallback and cooperative stop checks. `library/SmallIntegerCore.st` **50**; `library/Numerics.st` **73** establishes separate Integer/SmallInteger classes. | One Python `int` semantics across small/large representations; overflow must widen, including deopt and C conversions. Implement floor division/modulo signs, unbounded bitwise operations/shifts, hashing, parsing/format limits, `bool` subclass behavior, integer protocols/subclasses, `__index__`, modular power and native `PyLong` views. CPython `Objects/longobject.c` **6,874**; `Include/cpython/longintrepr.h` uses 15/30-bit binary limbs, not the donor's representation. Fast tagged machine integers are an internal optimization, not a second Python-visible type. |
| Dict-backed attributes and `__slots__` | Fixed named fields, class-state cells, layout migration and per-field kinds (`ST:vm/source_instances.inc`, `source_class_state.inc`, `source_reshape.inc`, `field_kinds.inc`). | Preserve writable/materializable `__dict__`, key-sharing/split dictionaries, missing/deleted fields, `__getattribute__`/`__getattr__` and `__setattr__`, slot member descriptors, inherited layouts, weakref/dict slots and extension `tp_dictoffset`/member offsets. Layout specialization must invalidate on dictionary exposure/mutation. CPython `Objects/dictobject.c` **7,761**; `Objects/object.c:_PyObject_GenericGetAttrWithDict`; `Objects/typeobject.c` slot construction near lines 3,798–4,509. |
| Descriptors and MRO | Direct Object→ClassHot and superclass method lookup; `ST:vm/vm.inc:98–108`, `INV-REP-3`; source send caches. | C3 multiple-inheritance MRO and conflicts; metaclasses; descriptor binding and data-descriptor precedence over instance dictionaries; non-data descriptors afterward; dynamic base changes, `super`, reflected numeric dispatch and corresponding C type slots. Properties can execute arbitrary Python. CPython `Objects/typeobject.c` **12,199**, `mro_implementation_unlocked`, `find_name_in_mro`; `Objects/descrobject.c` **2,082**; generic lookup in `Objects/object.c:1670` onward. A single receiver-type guard cannot prove all of this. |
| Generators, coroutines and async generators | Heap closures plus parkable, resumable Process frame chains (`ST:vm/source_closure.inc`, `sched.inc`, `snap_state.inc`). | Python-visible suspended activation objects; `send`, `throw`, `close`, `yield from`, delegation return values, `GeneratorExit`, StopIteration conversion, `await`, async iteration/finalization, `contextvars` and generator/coroutine introspection. A generator is not an independently scheduled Smalltalk Process. CPython `Objects/genobject.c` **2,416**, especially `gen_send_ex2`, close/throw paths; `Python/bytecodes.c` and `Lib/{asyncio,contextvars.py}` supply further contracts. |
| Exceptions with tracebacks | Handler/unwind machinery and protected cleanup; `ST:library/ObjectExceptions.st` **113**, `vm/source_runtime_failure.inc` **380**, `sched.inc:sc_unwind_walk`, SSA protection/reconstruction code. | Python exception classes/objects, active versus handled exception state, chaining/cause/context, source locations, traceback/frame retention, `finally`/context-manager suppression, exception groups/`except*`, and native `PyErr_*` indicators translated across callbacks. CPython `Objects/exceptions.c` **4,519**, `Python/{errors,traceback}.c`, `Include/cpython/pyerrors.h`. Non-local returns and resumable Smalltalk exceptions are not equivalent behavior. |
| Frame introspection | Debug Context views while active, detached read-only history; `ST:vm/dbg.inc` **1,383** + `source_context.inc` **168** = **1,551**; `INV-DBG-5`. | `sys._getframe`, `inspect`, frame/code attributes, current locals proxies, cells, traceback chains, tracing/profiling and monitoring events require Python observation points and stable materialized identities. CPython `Objects/frameobject.c` **2,439**, including `FrameLocalsProxy`; `Include/internal/pycore_interpframe.h`, `Python/instrumentation.c`, `Lib/inspect.py`. Donor detached Context semantics cannot be adopted as Python's locals/tracing semantics. |
| Full non-limited C-API | Donor primitive table and internal assembly ABI; no CPython extension ABI (`ST:vm/vm.inc` primitive/register contracts; `docs/CONVENTIONS.md`, zero-libc rule). | Exported functions/data/types, public struct and macro behavior, reference stealing/borrowing, native allocation APIs, subtype/type-slot machinery, buffer protocol, capsules, errors, imports/module state, vectorcall, thread-state/TLS, callbacks and finalization. NumPy adds dtype/ufunc/scalar/array behavior and native library dependencies. Inspect `vendor/cpython/Include/{Python.h,object.h,cpython/*}`, `Python/{importdl,pystate}.c`, and NumPy `_core/{include/numpy,src/multiarray,src/umath}`. This is the largest architectural boundary; no measured donor rate establishes its completion cost. |

Full API versus ABI is an open specification detail, not permission to weaken the
goal. An unmodified NumPy **binary** targets a particular CPython minor version,
platform and regular/free-threaded ABI; an unmodified source build also needs an
explicit header/configuration target. CPython's
[API stability documentation](https://docs.python.org/3.14/c-api/stable.html)
distinguishes the full API from the Limited API and limits ordinary ABI guarantees
to appropriately matched builds. Record the chosen NumPy artifact, headers,
dependencies, flags and suite inventory before asserting “passes”. A source
rebuild with custom headers does not demonstrate compatibility with an existing
CPython wheel.

M:N scheduling particularly needs a C-stack rule: an attached thread state is
required even without a GIL, and native calls can detach and reattach. Retain
worker affinity while an extension's C stack/TLS is live, or provide an equally
concrete alternative before migration. The donor's practice of recovering
worker identity from its own native stack cannot be blindly extended to foreign
threads. Evidence: `ST:vm/worker_state.inc`, `sched.inc`, and
[CPython thread states](https://docs.python.org/3.14/c-api/threads.html).

## 4. How fast the donor was built

### Dates, commits and what they measure

The first reachable and first-parent commit is
**`d39aea8e363ce8868321c847da205d1f4b616318`**, **2026-08-25 02:19:44 UTC**,
“init: design packet v3 under docs/, agent ground rules”. At the measured head,
**68.9759 elapsed 24-hour days** had passed. This starts with a design packet,
not with evidence of zero prior research or zero donor-library work.

| History population | Commits | Dates with commits | Rate denominator and rate |
|---|---:|---:|---|
| All reachable commits, including merged branch work | **10,504** | **69** | **150.06/calendar date** over 70 touched dates; **152.29/elapsed 24-hour day** |
| First-parent history | **4,289** | **67** | **61.27/calendar date**; **62.18/elapsed 24-hour day** |
| Complete UTC dates Aug 25–Nov 1, all reachable | **10,471** | 68 active dates | **151.75/day** over 69 dates; avoids the partial Nov 2 numerator |
| Complete UTC dates Aug 25–Nov 1, first-parent | **4,283** | 66 active dates | **62.07/day** over 69 dates |
| Last 14 complete dates, Oct 19–Nov 1, all reachable | **4,070** | 14 | **290.71/day** |
| Same 14 dates, first-parent | **598** | 14 | **42.71/day** |

Reproduction: `git ... log origin/master --format='%H %cI %s'`, and the same
with `--first-parent`; group by UTC **committer** date. First-parent dates were
monotonic in this census. Dates/counts above deliberately distinguish total
reachable work from master integration. The original Aug 25–Nov 1 distribution
has a 4–551 commits/day range across active dates; the busiest date is Oct 26
(551). First-parent counts include direct commits and merges and are **not** a
count of independent shipped features.

The final history's author fields record **10,418** commits under the primary
author identity and **86** under an assistant identity.
That does not measure who typed the code: the project dispatch/review process is
documented in `ST:AGENTS.md`, `docs/06-AGENT-PLAYBOOK.md`, and the optimizer
design's independent-candidate/review record. Git supplies no reliable concurrent
agent count, agent-hours, model mix or human supervision hours. Those quantities
are **unverified**. The observed unit here is **total repository output per
elapsed team day**, including review, waiting, rework, imports and unrelated
tracks. Dividing by an imagined number of agents, or multiplying by more agents,
would not be a measured estimate.

### Retained lines over time

For this table, dates are end-of-UTC-day first-parent source states, except the
partial Nov 2 cutoff. `VM` is only direct `vm/*.asm`/`vm/*.inc`; it includes
native compiler/CLI/tool code and historically included tests before their
relocation. `fasm all` includes assembly tests and frozen prototypes. `ST other`
is every tracked `.st` outside `imports/`, including library, benchmarks,
fixtures and the ANSI donor suite; it does **not** mean newly authored runtime.

| UTC date | fasm all | VM | VM tests under `vm/tests/` | Smalltalk all | ST `imports/` | ST other | `library/*.st` | Ticket files |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 2026-08-25 | 10,361 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 2026-08-31 | 64,803 | 25,952 | 0 | 0 | 0 | 0 | 0 | 0 |
| 2026-09-07 | 151,251 | 109,542 | 0 | 886 | 0 | 886 | 207 | 87 |
| 2026-09-14 | 196,208 | 43,734 | 110,758 | 17,511 | 14,996 | 2,515 | 682 | 189 |
| 2026-09-21 | 228,048 | 78,732 | 107,600 | 18,832 | 12,947 | 5,885 | 911 | 313 |
| 2026-09-28 | 242,000 | 99,656 | 100,628 | 24,165 | 13,024 | 11,141 | 5,140 | 453 |
| 2026-10-05 | 251,030 | 104,501 | 102,660 | 31,837 | 13,024 | 18,813 | 5,942 | 560 |
| 2026-10-12 | 251,771 | 146,426 | 61,338 | 46,080 | 13,079 | 33,001 | 6,161 | 792 |
| 2026-10-19 | 278,271 | 173,762 | 60,498 | 55,035 | 13,136 | 41,899 | 6,753 | 1,062 |
| 2026-10-26 | 279,845 | 185,923 | 49,816 | 115,483 | 10,019 | 105,464 | 26,409 | 1,356 |
| 2026-11-01 | 292,714 | 207,100 | 41,508 | 633,834 | 521,125 | 112,709 | 27,597 | 1,611 |
| 2026-11-02, cutoff | **292,425** | **206,811** | **41,508** | **633,871** | **521,125** | **112,746** | **27,597** | **1,612** |

Oct 5 had no first-parent commit; its source state is Oct 4's
`6673276ca5a21621ccd76790c0b3d2113e65f0b5`. At cutoff the tree contains
**405 fasm files**, **210**
direct VM source files, **7,995 `.st` files**, **85** library `.st` files, and
**1,612 `.tickets/*.md` files**. Ticket YAML status counts are **1,563 closed,
40 open, 9 in_progress**; “closed” is not synonymous with implemented. The
Oct 26 direction record explicitly says 107 planning-era tickets were closed
with reopen triggers (`ST:docs/vm/flat-layout-direction-PLAN.md:58–62`).

Tickets were first introduced on Sep 2 (`85929cb4c`, triage of seven tickets).
Their historical populations were reconstructed from first-parent
`--name-status --no-renames -- .tickets/` additions/deletions, then reconciled
against the current `ls-tree` inventory. No ticket-management operation was performed. The
current count is a tree count, not a count of every historical ticket edit.

Reproducible line-history script, taking the Smalltalk VM source root as its sole argument:

```sh
if [ "$#" -ne 1 ]; then
  printf 'Usage: %s SOURCE_ROOT\n' "$0" >&2
  exit 2
fi
source_root=$1
git -C "$source_root" log origin/master \
  --first-parent --reverse --diff-merges=first-parent --no-renames \
  --numstat --format='@@@%H %cI' -- '*.inc' '*.asm' '*.st'
```

Accumulate numeric additions/deletions per path/category, flushing the retained
totals at each day boundary. Do not sum numstats over every reachable branch
commit: that double-counts merged work. For current per-file sizes, enumerate
with the permitted `ls-tree` command and count each `show origin/master:<path>`
blob separately; concatenating blobs lacking final newlines can undercount.

Interpretation of the counts:

- The Sep 7→14 VM decrease is substantially test relocation, not a destroyed
  runtime: the new `vm/tests/` population is 110,758 lines. The table preserves
  both columns to expose that classification change.
- **521,125 / 633,871 = 82.21%** of final Smalltalk lines are under `imports/`.
  Other directories also contain imported code: the ANSI suite is 24,603 lines,
  AWFY is 5,240, and `library/` contains adapted donor bodies. Counting 633,871
  lines as agent-authored implementation would be unsound; even excluding
  `imports/` does not solve authorship attribution.
- Mature-period direct VM growth is **(207,100−99,656)/34 = 3,160.1 lines/day**
  from Sep 28 to Nov 1, and **(207,100−146,426)/20 = 3,033.7/day** from Oct 12
  to Nov 1. These are retained net assembly growth, not typing speed, verified
  feature throughput, or universal rates for Python source.
- At the earlier cutoff `f9b78b3a9`, first-parent numstats contained **385,446
  direct-VM additions and 178,867 deletions**, net 206,579. That is substantial
  churn, with relocations and rewrites included. The final head differs; this
  explicitly dated observation is not presented as its churn census.

### Milestone landing evidence

“Landed” here means a first-parent integration commit, not a proposal date or an
unmerged implementation timestamp. Day offsets use the first commit above.

| Mechanism/checkpoint | Earliest relevant implementation evidence | Master landing and elapsed days | Limits |
|---|---|---|---|
| Template engine choice | Frozen prototype engine comparison | **2026-08-28 02:50:31**, `c989b2fe5`, **3.021 d**, “merge: o template engines - engine 3 wins per complexity” | Prototype verdict; not Python or complete Smalltalk. |
| VM skeleton with vertical acceptance | Assembled heap, execution, collection, store and debug slices | **2026-08-31 04:54:43**, `642316801`, **6.108 d**, “skeleton complete - vertical acceptance + PIE cross-load conformance” | `ST:README.md` still bounds language/production coverage. |
| Source CLI | `7a52cebdd`, “load ANSI Smalltalk and evaluate class methods” | **2026-09-07 01:01:21**, same first-parent commit | Source execution was later than the initial skeleton. |
| First optimizing tier | `053eb374e`, **Oct 10 04:13:24**, single-frame pipeline | **2026-10-10 04:32:50**, `a563caca5`, **46.092 d**, slice a; slice b `ac160d2f3` at 05:18:02; default-on `cffc8c978` at 08:07:40 | Requirements/design landed Oct 8 03:17–03:19 (`e732dd5e9`); first tier arrived about 2.05 d later, on an existing VM. |
| Guard deoptimization | **2026-10-10 04:16:58**, `42b846331`, “deoptimize failed arithmetic and boolean guards” | Contained in the **Oct 10 04:32:50** slice-a landing above | No earlier commit whose message says deoptimization was found; preexisting canonical/native boundary restoration is distinct. |
| Block SSA and reconstruction maps | `0f2fa592c`, **Oct 11 18:25:06**, leaf loops through block SSA | **2026-10-11 18:29:46**, `c1bbf5a32`, **47.674 d**; default loop route **Oct 12 00:22:35**, `8564253ce`, **47.919 d** | The original optimizer and block SSA are different landings. Current source includes later, much broader coverage. |
| Initial raw Float fields | `27c6f4200`, Oct 16 15:12:34, SSA raw loads/stores | **2026-10-16 15:32:07**, `9b7334f44`; feedback specialization first landing **Oct 17 14:34:47**, `95b369bb3` | Initial specialization-off stage, then feedback; subsequent review fixes matter. |
| Flat-layout direction | Owner direction recorded, not general flat objects implemented | **2026-10-26 18:36:01**, `d84bf3cd8` | `docs/vm/flat-layout-direction-PLAN.md` separates stages. |
| Collection storage strategies | Constructor/strategy source and tests | **2026-10-30 00:37:34**, `44d19e786`, **65.929 d** | Follow-up `f02b2a53d` fixes strategy semantics; implementation is not all possible layouts. |
| Value-layout design | `0e72f159f`, Nov 1 17:49:29 | **2026-11-01 17:50:15**, `4b4e7ac7f` | Design of L1–L4 is not implementation of L1–L4. |
| L1 full-range raw Float slots and layout-driven GC | First L1 landing `eb5505e21`, **Nov 1 23:52:39** | Further landing **2026-11-02 00:46:09**, `955af6fad`, **68.935 d** | Source/invariants confirm identity-free Floats and raw-layout scanning. General Struct/value inline composite values remain **unverified as implemented**; `docs/vm/value-layout-DESIGN.md` stages L2–L4 separately. |

This answers the flat-layout date without inventing a single “all flat objects
finished” event. Ordinary named fields were already contiguous after the header
(`ST:vm/vm.inc:83–109`); raw Float fields, homogeneous arrays and future inline
composite values are different mechanisms.

### Speed evidence is useful, bounded evidence

`ST:vm/results/status-2026-11-01/README.md` and `summary.csv` record a run of
**`149436e03d9ee0460975808176c634f23f4bc483`**, not the final census head:
AWFY geometric mean **VM time / Go time = 1.43**, **VM / Java = 2.02**, and
**VM / Pharo = 0.225**. Thus the VM took 43% longer than Go and roughly twice
Java's time, while running about **4.44× Pharo's speed**, on that suite/host.
Those ratios demonstrate a productive optimizer effort, not Python performance
or multicore speedup.

The same status record reports `smoke=0`, **`test=2`**, **`test-heavy=124`**,
`quick=1`, `gate=1`; six listed focused re-tests subsequently exited zero.
This is not an all-green release certificate. The donor's build rate includes
an explicit pre-alpha fix-forward process (`ST:docs/ROADMAP.md`, Keeping it
correct). Lucifer's “production Python behaves correctly” goal needs additional
acceptance work; treating 69 days as time to production-quality Python would
discard the evidence's own qualification.

## 5. Conditional estimates for Lucifer's first three milestones

These are **unverified planning scenarios**, not measured Python completion
times or statistical confidence intervals. There is no donor counterpart for
the full extension ABI, and lines alone cannot bound a compatibility failure.
Use them to budget experiments and revise after evidence, not to announce a
release date. A failed ABI/GC prototype can take the work outside these ranges.

Measured calibration: about **3.0–3.2k retained direct-VM assembly lines per
elapsed day** in the mature periods above. Use an explicit **2–4k/day combined
implementation rate** around that observation. New Python code may have a very
different line density; applying that rate to mixed Python/native work is an
assumption. Imported/copied lines are not new output. A second assumption,
**1.5–2.5× elapsed integration/compatibility allowance**, covers serial design
decisions, review, native-boundary repair and acceptance beyond code growth;
the donor's history does not independently measure that multiplier.

Formula, with `D` the increment of new or substantially reworked implementation
lines **after** component copying:

`incremental elapsed team days = (D / 4,000) × 1.5 ... (D / 2,000) × 2.5`.

| Proposed milestone, cumulative acceptance | Assumed incremental D; why this order of work | Incremental elapsed team days | Cumulative elapsed team days |
|---|---|---:|---:|
| **M1: full-C-API/no-GIL compatibility checkpoint**. Exact chosen CPython ABI; full API surface/behavior contract from the initial architecture; unmodified NumPy artifact's own complete applicable suite passes, without a GIL, with matched baseline skip accounting. Python-built core built-ins, import/module machinery, all seven gap areas needed by that stack, native thread/callback/root/GC correctness and multicore stress are acceptance work. No limited-API interim public release is counted. | **120–240k**, unverified. Python frontend/core semantics, ABI/lifetime/loader machinery, Python built-ins and native-boundary integration are mostly new even though template/SSA/backend tooling is available. Donor comparison: 206,811 current direct-VM lines plus 27,597 library lines, with a substantially smaller foreign-ABI problem. | **45–300** | **45–300** |
| **M2: broader production Python/stdlib compatibility**. Expand beyond the NumPy/pytest path to documented CPython regression coverage, imports/resources, built-in protocols, generators/async/tracing, exceptions, threading and representative extension/package applications; built-ins and stdlib mostly Python compiled by the JIT. Carry forward M1's full API and no-GIL bar, including arbitrary re-entry and concurrent mutation tests. | **80–160k**, unverified. Remaining library breadth and protocol/extension edge cases; CPython reference files in §3 show why NumPy passing is not the entire Python surface. Testing/triage may dominate line growth, which is part of the allowance rather than an assertion that tests need this many implementation lines. | **30–200** | **75–500** |
| **M3: broad measured JIT and multicore performance checkpoint**. Publish correctness-preserving comparisons against CPython, PyPy and free-threaded CPython over AWFY Python ports, pyperformance, C-extension/object-heavy/async/application cases and worker scaling; report startup, warmup, memory, GC and native/JIT time separately. Choose numerical speed targets before calling this milestone complete. | **40–100k**, unverified. Adapt SSA optimization, ownership/escape and representation proofs, Python-specific invalidation, call/return and scheduler/native-affinity tuning. Snapshot support, if separately chosen, is extra scope. | **15–125** | **90–625** |

The size budgets are explicit estimates, **not counts of a known missing-code
inventory**. The low ends require reuse to work without an object-model restart;
the high ends are bounded scenarios, not a guaranteed upper bound. NumPy passing
is in M1 because the owner put it at the public entrance; it is not postponed to
fund an easier interpreter/JIT demonstration. Internal experiments can precede
M1, but do not earn a CPython-compatible/no-GIL release claim.

Assumptions needed for these ranges:

- Similar combined agent/review/host availability to the measured donor effort,
  with native/semantic work proceeding where dependencies permit. The donor's
  final 14-day rate is 42.71 first-parent integrations/day, not 290.71 separately
  reviewed features/day. No independent-agent speedup factor is applied.
- One selected minor-version/platform/ABI target and a pinned NumPy/dependency
  environment; architecture and acceptance do not silently broaden to all wheels,
  all platforms or all CPython historical versions at once. Exact target remains
  an owner decision; the fixed full-API requirement is unchanged.
- C-visible lifetime/identity and native thread participation are resolved before
  large compiler/library transfer. A mirrored representation that repeatedly
  breaks direct writes invalidates the line budget rather than just slowing it
  by a known percentage (§2).
- Existing library implementations may be studied/ported where appropriate, but
  the built-ins/stdlib implementation remains mostly Python. Copying CPython's
  C object implementations wholesale is not assumed to satisfy that requirement.
- Correctness runs, stress, review and benchmark serialization remain in elapsed
  time. Their actual share and the achievable semantic defect rate in Lucifer
  are **unverified**; this report executes none of those workloads.

The donor did move from an optimizing-tier design to a first integrated tier in
about two days, and to initial block SSA the following day. That is evidence for
fast bounded compiler work **on an established compatible execution spine**.
It is not evidence for a two-day Python C-API or for the first six-day skeleton
already having source-language/stdlib coverage. Milestone timing should be
re-estimated after the first native pointer/refcount/thread-state tests and after
NumPy's first full failure census; use measured failure closure and integration
cycle times alongside retained code growth.

## 6. Repository recommendation and unresolved decisions

**Use a separate Lucifer repo with selected copies.** Keep a donor revision/path
record adjacent to copied components and preserve existing third-party notices.
Bring over language-neutral writer/measurement/test support first; then backend
pieces only through a Python state/ABI interface. Existing independent
build and benchmark admission tools should remain shared tools. The donor has more than 200k lines
of tightly coupled top-level VM assembly, and source templates explicitly name
its frame, PCB, class, opcode, GC and helper contracts (§1). A shared runtime core
now would force Python into those contracts or add representation conditionals
through hot paths. That cost is a design inference from the coupling, not a
measured comparison of two implemented repositories.

A future shared **small component** can be extracted when both implementations
have a demonstrated identical contract and independent conformance checks—for
example Linux perf record encoding or an x86 operand encoder. Do not treat the
entire SSA directory as already language-independent: it embeds Smalltalk
numeric, object, source and materialization semantics (`ST:vm/optimizer_ssa*.inc`).

| Open decision | Evidence and next reviewable result |
|---|---|
| Exact meaning of the unmodified NumPy build | Pin wheel/build identity, CPython minor, regular versus free-threaded ABI, dependency versions and baseline full-suite inventory. CPython `Include/object.h` gives materially different headers; NumPy declares no-GIL support but does not erase ABI selection. Recommend investigating a 3.14 free-threaded artifact first; owner selection is unresolved. |
| Canonical exposed-object representation and lifetime | Compare a nonmoving C-compatible representation with any proposed dual view using direct header/container writes, borrowed refs, `Py_INCREF`/`Py_DECREF`, finalizers, object arrays and buffers. Deliver an ownership/root/reclamation contract before scaling transfer. Feasibility is unverified (§2). |
| Concurrent reads, writes and invalidation semantics | Specify container operations, atomic publication, lock ordering, reentrant descriptors/finalizers and C macro preconditions. Donor `INV-REP-4` permits binding retention until a safepoint; that alone does not establish Python concurrent-mutation behavior. Use free-threaded CPython source/behavior as the comparison, without a GIL fallback. |
| Python threads versus lightweight tasks | Define OS-thread/TLS affinity, foreign-thread attachment, `PyGILState_*` compatibility, blocking C calls and main-thread signals. Decide which activations can migrate and when; preserve `threading`/`contextvars` observations. Donor `sched.inc`/`runqueue.inc` establish only the internal Process side. |
| Observation and deopt points | Write exact Python frame/code/traceback/local/exception/generator states and effects before importing optimizer passes. Prove no duplicate side effects after a guard fails and no lost aliases after GC/materialization (`INV-EXE-1`; CPython frame/gen/exception sources). |
| Python implementation bootstrap and primitive boundary | Inventory native primitives needed for ABI-backed storage, allocation, atomics/locks, numeric leaves, C calls, buffers, imports and OS I/O; show Python implementations of representative built-ins using them. “Mostly Python” needs an auditable denominator; its exact threshold is unresolved in README. |
| Python identity versus flat layouts | Keep C-exposed identity and existing-object aliases; determine scalar-replacement eligibility, `__slots__` layouts and list strategy widening. Do not import identity-free Float or future value-class semantics. Transfer of general inline composite values is unverified (`INV-REP-1/11`, `value-layout-DESIGN.md`). |
| Persistence scope | Lucifer README does not require images. Decide separately whether M1/M2 include no snapshots, managed-state snapshots with explicit external-resource rules, or a broader extension contract. The third is not demonstrated by the donor store. Exclude it from estimates unless explicitly selected. |
| Acceptance and performance targets | NumPy suite accounting is fixed; select CPython/package coverage, stress conditions and M3 numerical targets. Keep failures/timeout/skip counts and confidence/noise controls visible. The Nov 1 donor result shows why headline speed and production acceptance need separate evidence. |

Verification of this research: read-only source/history/tree inspection, current
file counts, first-parent line reconstruction, ticket inventory/status counting,
and inspection of existing benchmark/status records. No Smalltalk or vendor
runtime was executed. Claims of Python transfer success, future delivery dates,
general flat composite values, universal legacy-extension thread safety and extension
snapshot recovery remain **unverified**.
