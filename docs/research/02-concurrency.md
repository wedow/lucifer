# Concurrency: no-GIL semantics that production Python depends on

Research date: 2026-11-02. Design input, not an implementation or a claim that
Lucifer passes these tests. The fixed requirements are full, non-limited C-API
support from day one, NumPy's own tests against an unmodified NumPy build, no
GIL, and built-ins/stdlib mostly written in Python over native primitives
([Lucifer README](../../README.md)). Recommendations below preserve those
requirements; unresolved compatibility problems are recorded rather than
assumed solved.

## 0. Evidence and interpretation

| Material | Inspected revision / scope |
|---|---|
| CPython | `vendor/cpython`, `ebf955df7a89ed0c7968f79faec1de49f61ed7cb`, Python **3.14.0**, 2025-10-07; source, tests, docs and CI read, never executed |
| Ecosystem tracker | `vendor/free-threaded-compatibility`, `53b7eaf3f5fa312e3deef3b944af35074854af1e`, 2026-10-21; all 152 project rows counted |
| NumPy | `vendor/numpy`, `df9913aa4ea20c33728fbc434acc66c3285f12a2`; source, release notes, tests and CI read |
| The Smalltalk VM | Only read-only source blobs and tree inventories at `origin/master`; `docs/04-SYSTEM-MAP.md`, `docs/05-INVARIANTS.md`, scheduler, fairness, wait, GC and worker-lifetime docs |
| Web | PEPs, current versioned Python documentation, upstream issue reports, package release notes and the pytest plugin README; URLs attached to claims |

**Verified** below means source inspected, counts computed, or a published
statement checked. An upstream reproducer is evidence of a report, not a local
reproduction. **Proposal** means a Lucifer design or future validation rule.
**Unverified** means evidence is missing; it does not mean safe. No builds,
package installations, reference-code execution or benchmarks were performed.

Source shorthand: `Lib/`, `Objects/`, `Python/`, `Include/`, `Modules/`,
`Doc/` and `Tools/` refer to `vendor/cpython/`; `numpy/` refers to
`vendor/numpy/numpy/`. Smalltalk `docs/` paths in §3 refer exclusively to the
`origin/master` blobs described above.

There are three different requirements: memory safety; atomicity of an
individual operation; and ordering/publication between operations. Internal
container locks do not make a multi-operation algorithm atomic. Moreover,
Python callbacks can re-enter an apparently primitive operation through
`__hash__`, `__eq__`, descriptors or destruction. The old FAQ's bytecode-based
atomicity list needs those qualifications even with the GIL
(`vendor/cpython/Doc/faq/library.rst:362`; the same file warns about `__del__`).
Use the observable operation, operand types and callback boundary as the unit
of compatibility, not a particular CPython bytecode or JIT instruction count.

## 1. What existing code relies on

The table separates an actual dependency from a stronger guarantee that code
must not assume. These are CPython implementation expectations unless a
documented library contract says otherwise. CPython 3.14.0 itself explicitly
declines a historical general guarantee for concurrent built-in mutations
(`vendor/cpython/Doc/howto/free-threading-python.rst:80`). Later 3.14
documentation adds a more explicit guarantees reference; do not read that
back into every 3.14.0 implementation path
([current HOWTO](https://docs.python.org/3.14/howto/free-threading-python.html#thread-safety)).

| Operation / behavior | Dependency and limit under the GIL | Evidence in actual code or reports |
|---|---|---|
| `list.append(x)`, `list.pop()` | An exact-list append publishes one intact element without losing another append; a pop removes/returns one element or raises `IndexError`. Producer/consumer code relies on object lifetime and structural integrity. `if xs: xs.pop()` is two operations and can race. Append return does not order an unrelated flag in another object. | `Lib/test/test_importlib/test_threaded_import.py:39` collects completion IDs with `done_tasks.append`; `Lib/test/test_free_threading/test_dict.py:racing_creation` uses append/length as a handshake. [bpo-44962](https://bugs.python.org/issue44962) shows the real failure of check-then-pop in asyncio's weakref bookkeeping, even with a GIL. Paths in this table are under `vendor/cpython/` unless otherwise specified. |
| `dict[k]`, `dict.get(k)`, `dict[k] = v` | A lookup returns a live object or absence; an individual exact-dict update does not damage the table or expose an unowned value. Hash/equality can execute Python. `get` followed by `set`, incrementing a value, and setting two keys are not one transaction. `defaultdict` factories and subclasses add further callback boundaries. | `Lib/weakref.py:WeakValueDictionary` and `Lib/importlib/_bootstrap.py:_module_locks` use dictionaries with explicit lifetime/locking rules; `Lib/test/test_dict.py:test_setitem_atomic_at_resize` tests re-entry at resize. [bpo-43498](https://bugs.python.org/issue43498) documents a ProcessPoolExecutor iteration failure during dictionary growth. |
| `dict.setdefault(k, v)` | For ordinary non-reentrant keys, competing calls on an absent key agree on the installed winner unless another operation removes/replaces it; computing `v` happens before the call, so duplicate construction is possible. The lookup/insertion must not be split into separate unlocked Python operations. No transaction extends into `.append()` on the returned value. | [bpo-13521](https://bugs.python.org/issue13521), especially [msg148782](https://bugs.python.org/msg148782), explicitly records deployed code relying on atomicity and the second `__hash__` problem. `Lib/test/test_dict.py:test_setdefault_atomic` checks one hash/equality lookup. |
| Iteration while mutating | List iteration is not a snapshot: it can skip/revisit positions as contents change. Dict/set structural changes can raise `RuntimeError`; detection is not a transaction or a promise to detect every change. Iterating a shared container with separate iterators differs from concurrently advancing one iterator. A shallow built-in copy can be a useful snapshot; `list(arbitrary_iterable)` cannot promise one. | [bpo-43498](https://bugs.python.org/issue43498) records hangs following `dictionary changed size during iteration` in `_ExecutorManagerThread`, fixed using a copied view of values. `Lib/_weakrefset.py:__iter__` iterates `self.data.copy()`. `Lib/test/test_free_threading/test_iteration.py` deliberately allows some duplicates/missing values for shared iterators. |
| `+=`, counters and check-then-act | `n += 1`, `obj.n += 1`, and `d[k] += 1` combine read, arithmetic and write; the GIL is not an application lock for this sequence. Some CPython versions/specializations make simple demonstrations appear reliable, which is not a contract. `__iadd__` can run arbitrary code; an in-place list operation is different from the entire augmented assignment. | `Lib/queue.py:Queue.task_done` updates `unfinished_tasks` while holding a condition's mutex. `Lib/test/test_free_threading/test_type.py:test_attr_cache_consistency` uses **one** writer for `C.x += 1`, rather than asserting lossless increments from competing writers. The non-atomic counter/dict examples are explicit in `Doc/faq/library.rst:392`. |
| Set `add`, `discard`, `remove`, `pop`, union/intersection/update | Individual table operations must preserve integrity and live references. Two-set operations need a consistent locking protocol. `if x not in seen: seen.add(x)` does not promise unique work. Arbitrary iterables/hash/equality can re-enter; an entire bulk operation is not generically atomic. | `Lib/_weakrefset.py:add`, `_remove`, `pop`, `difference_update` use exact set primitives. [bpo-44962](https://bugs.python.org/issue44962) connects weakref container races to real asyncio task creation. `Lib/test/test_free_threading/test_set.py:test_repr_clear` races repr against clearing. |
| `collections.deque` | Endpoint append/pop are the specifically documented thread-safe queue operations. Empty-check/pop and `d.appendleft(d.pop())` are compound. Bounded-deque eviction can destroy an element and run callbacks after structure is consistent. Do not extrapolate to arbitrary comparison/iteration operations. | `Lib/asyncio/base_events.py:call_soon_threadsafe` appends to `_ready` and wakes the loop; `_run_once:2028` explicitly describes its no-lock idiom. [bpo-15329](https://bugs.python.org/issue15329) discusses precisely which deque methods qualify and destructor re-entry. |
| `queue.Queue`, `SimpleQueue` | Queue put/get, bounded capacity, blocking, wakeup and join/task accounting depend on **explicit locks and conditions**, not just deque atomicity. `empty()`/`qsize()` are observations, not reservations. Queue's ordinary mutex is not a reentrancy guarantee. | `Lib/queue.py:Queue.__init__` shares one mutex among three conditions; `_PySimpleQueue` adds a semaphore. [bpo-14976](https://bugs.python.org/issue14976) reports logging/GC/signal/weakref deadlocks and SQLAlchemy connection-pool use. The C SimpleQueue has a stronger reentrancy contract (`Doc/library/queue.rst`). |
| Attribute assignment and lookup | Plain instance slot/dict replacement must publish a valid owned reference; concurrent readers must not use stale type/shape caches. `obj.a = v` does not atomically update another field, execute a property transaction, or implement singleton initialization. Descriptors and custom `__setattr__` run Python. | `Lib/test/test_free_threading/test_races.py` covers slots, inline instance values, dict hints and dict stores. `test_type.py:test_attr_cache_consistency[_subclass]` requires a type lookup not to regress behind an earlier direct dict read. Real metaprogramming paths occur in `Lib/enum.py` and `Lib/dataclasses.py`. |
| Import locking | Imports of the same name normally coordinate so a competing importer waits for initialization. Different modules can initialize concurrently. Recursive/circular imports can expose partially initialized modules; `sys.modules` access is not a general initialization barrier. Failure must clear the failing entry and release waiters. | `Lib/importlib/_bootstrap.py:_ModuleLock`, `_get_module_lock`, `_lock_unlock_module`, `_find_and_load`; `Lib/test/test_importlib/test_threaded_import.py` tests parallel initialization, finders/hooks and circular imports. It derives from a real threaded import bug reproducer. |
| Module initialization | Python module execution must follow the import protocol; C extension init/state needs additional protection. A successful normal import should not publish a half-populated module to another non-recursive importer. Reload, direct `sys.modules` reads and extension global state do not inherit a once-only process-wide guarantee. | `Lib/importlib/_bootstrap.py:_load_unlocked` inserts into `sys.modules` **before** `exec_module`, tracks `_initializing`, and removes the entry on failure. `Python/import.c` has separate single-/multi-phase extension paths and GIL checks; `Lib/test/test_importlib/test_threaded_import.py:test_parallel_module_init` exercises the ordinary guarantee. |
| Class creation / class mutation | Class bodies, metaclasses, `__set_name__` and `__init_subclass__` are arbitrary Python, not a global atomic transaction. Type readiness, MRO/subclass registration and cache invalidation must remain coherent. Assigning the resulting class name is distinct from executing its body or registering it in another registry. | `Lib/enum.py:EnumType.__new__` and descriptor `__set_name__`; `Lib/dataclasses.py:Field.__set_name__`; `Lib/test/test_subclassinit.py`; `Objects/typeobject.c:type_new_impl`, `type_ready`, subclass management and `BEGIN_TYPE_LOCK`; concurrent type/cache tests in `Lib/test/test_free_threading/test_type.py`. |
| Refcount-driven finalization | Acyclic CPython objects often finalize at last release, making resource cleanup and weak caches appear immediate. Finalizers can execute in the thread dropping the last reference, with arbitrary surrounding application locks held. Resurrection, cycles, shutdown and non-CPython implementations already defeat a universal immediate-finalization promise. | [bpo-14976](https://bugs.python.org/issue14976) includes SQLAlchemy weakref returns and logging from GC. `Lib/weakref.py:finalize` and `Lib/test/test_weakref.py` exercise observable callback timing. `Objects/object.c` deallocation and `Python/gc_free_threading.c` distinguish reference merging and cycle/finalizer work. |
| Generators shared across threads | Sequential handoff is valid; simultaneous execution must not run one generator frame twice. With the GIL, the executing-state check normally raises `ValueError` on conflicting `send`/`next`/`throw`/`close` while another invocation is active. This is rejection of concurrent execution, not a queue of next requests. | `Objects/genobject.c:gen_send_ex2` contains the error; ordinary generator tests are in `Lib/test/test_generators.py`. [GH-156351](https://github.com/python/cpython/issues/156351) supplies a concurrent-next crash reproducer for 3.13t/3.14t: the GIL-era check alone is insufficient without atomic acquisition of execution ownership. |
| `sys.settrace`, profiling, monitoring | `sys.settrace` is per-thread; enabling it does not serialize all trace callbacks across the process. Debuggers depend on correct frame/line/opcode state and safe instrumentation changes. The GIL formerly serialized most Python callback execution, so shared debugger bookkeeping may need its own locks. | `Lib/bdb.py` records a tracing thread and installs `sys.settrace`; `Lib/test/test_free_threading/test_monitoring.py` races tracing/profiling/monitoring setup and uses locks for collected traces. `Python/legacy_tracing.c:setup_tracing` stops the world to change thread tracing state. |
| Weakref callbacks / weak containers | A successful weakref dereference must obtain a strong live reference atomically relative to death. Callbacks invalidate weak state and can mutate containers/re-enter Python. Avoid running Python while holding raw weak-list locks. The GIL did not make a weak container's Python implementation a transaction. | `Lib/weakref.py:WeakValueDictionary` uses `_remove_dead_weakref`; `Objects/weakrefobject.c` explains its no-detach striped lock. [bpo-35615](https://bugs.python.org/issue35615) reports WeakValueDictionary copying failures in signal/slot code; [bpo-44962](https://bugs.python.org/issue44962) reports asyncio weak-container races. |
| Signals | Python handlers run in the main thread of the main interpreter, regardless of which OS thread receives a signal; only that thread installs handlers. Native signal delivery posts flags/wakeup, with Python execution deferred to a check point. Signals can re-enter ordinary Python between steps and do not make application locks reentrant. | `Modules/signalmodule.c:83`, `trip_signal:274` explains flag-before-wakeup ordering and the lost-wakeup bug **bpo-30038**. `Lib/test/test_signal.py`, `test_threadsignals.py`, `test_threading.py:InterruptMainTests`; [bpo-14976](https://bugs.python.org/issue14976) shows signal re-entry into Queue. |
| C extension static state | Legacy extensions commonly rely on entry with the GIL to protect process-global caches, free lists and mutable Python references. Per-container locks cannot protect unrelated C globals or non-atomic inline refcount operations. Releasing the GIL already required extensions to protect state accessed in that region. | `vendor/numpy/doc/source/release/2.1.0-notes.rst:115` says globals were removed, made thread-local or locked. `numpy/_core/src/multiarray/array_coercion.c:614` disables its five-entry global coercion-cache free list under `Py_GIL_DISABLED`. `numpy/_core/tests/test_multithreading.py` documents RNG, coercion, ufunc dispatch/cache and legacy dtype races. |

**Ordering to preserve (proposal).** Define a Lucifer memory model for Python
operations: per-thread program order, acquire/release publication through
Lock/RLock/Condition/Event/Queue and thread start/join, and linearization of
the explicitly supported single-object operations. Reads must obtain valid
objects; synchronized publication must expose initialized state. Do not permit
the JIT to hoist a shared read forever out of a synchronization loop or reuse
a stale attribute cache after a synchronized mutation. Do not promise that
unsynchronized `ready = True` publishes unrelated mutable data, or that the
GIL supplied a portable formal memory model. The concrete implementation
anchors are `Lib/threading.py`, `Lib/queue.py`, `Python/lock.c`,
`Include/cpython/pyatomic.h`, and the type-cache consistency tests above.

## 2. How free-threaded CPython supplies, and limits, these behaviors

### 2.1 Version boundary and measured cost

[PEP 703](https://peps.python.org/pep-0703/) establishes the optional build and
its mechanisms. [PEP 779](https://peps.python.org/pep-0779/) moves 3.14 to
supported-but-optional phase II; 3.13t was experimental. Neither means all
extensions are safe, all data races are fixed, or free threading is the
default. The following numbers are published measurements/criteria, not
Lucifer measurements and not directly interchangeable baselines.

| Evidence / baseline | Published result | Interpretation |
|---|---|---|
| [PEP 703 performance section](https://peps.python.org/pep-0703/#performance), prototype versus `018be4c` (3.12 immortal-object baseline), pyperformance **1.0.6** | One-thread overhead: **6% Intel Skylake, 5% AMD Zen 3**; multi-thread execution overhead: **8%, 7%** | Historical prototype overhead, **not** a 3.14 result and **not** parallel speedup. Biased RC and locking were major contributors. |
| `vendor/cpython/Doc/howto/free-threading-python.rst:145`, explicitly discussing **3.13** | About **40%** pyperformance overhead; specialization disabled; aim of 10% or less | This paragraph is stale in the vendored 3.14.0 documentation. Do not label it a measured 3.14 penalty. |
| [PEP 779 rationale/specification](https://peps.python.org/pep-0779/#rationale), 2025 proposal | About **10%** CPU overhead, about **3%** on macOS; **15–20%** memory overhead. Phase-II proposed ceilings: **15%** runtime and **20%** memory geometric mean | Status criteria and development-era observations; hardware/build/benchmark mix matters. |
| [Current 3.14 HOWTO](https://docs.python.org/3.14/howto/free-threading-python.html#single-threaded-performance), consulted 2026-11-02 | About **1% macOS aarch64 to 8% Linux x86-64** average pyperformance overhead | Later 3.14 documentation; exact patch/build revisions and raw distributions are not supplied there, so attribution beyond that range is **unverified**. |
| [Tensogram upstream benchmark](https://sites.ecmwf.int/docs/tensogram/main/guide/free-threaded-python.html#benchmark-results), Linux x86-64, 20 cores, NumPy 2.4.4, release build, medians of five runs | 3.14t uncompressed 1M-float32 decode: **396 → 776 → 1,352 → 1,841 ops/s** at **1/2/4/8 threads**; **4.65×** its own one-thread throughput. Ordinary 3.14: **408 → 432 → 425 → 293**. 3.14t encode: **595 → 717** at 1→8, only **1.20×** | Useful positive and poor-scaling examples in one primary report. Native Rust calls are individually single-threaded; Rust work releases the GIL on ordinary CPython too. These are mixed Python/native throughput results, not pure-Python interpreter speedups. CPU model, CPython patch number and uncertainty intervals are **unverified**. |

For future Lucifer measurements, compare same-version GIL/FT builds, the same
package source release and matched compiler settings, recording their distinct
ABI-specific artifact hashes; publish one-thread tax and
`throughput(N)/throughput(1)` separately, alongside shared-object contention,
latency and memory. The [Meta benchmark infrastructure](https://github.com/facebookexperimental/free-threading-benchmarking)
publishes raw pyperformance data and comparison baselines; its moving main
results include later CPython development versions, so they were not relabeled
as 3.14 release measurements here.

### 2.2 Mechanisms verified in the vendored implementation

| Mechanism | Source / observed implementation | Consequence for semantics and Lucifer |
|---|---|---|
| Per-object critical sections | `Include/internal/pycore_critical_section.h`, `Include/cpython/critical_section.h`, `Python/critical_section.c`: an uncontended `PyMutex_LockFast`, a thread-state stack, two-mutex address ordering, same-top-lock recursion optimization, suspension on thread-state detach/block, restoration of the top section | They protect a bounded operation, **not** an arbitrary callback-spanning transaction. Nested sections do not guarantee simultaneous possession of both locks; use the two-object operation. Any code after a suspension must revalidate borrowed state. |
| Lists | `Objects/listobject.c:list_resize`, `ensure_shared_on_resize`, `list_get_item_ref`, `list_item_impl`, `list_append_impl`, `list_pop_impl`; generated wrappers in `Objects/clinic/listobject.c.h` | Append/pop acquire the list lock. Resize allocates a replacement array and release-publishes its pointer. Optimistic item reads check size/capacity and try to obtain a strong reference, falling back to a locked read. Non-owner access marks the list shared so old storage can be retired safely. Ownership alone does **not** mean all mutations omit locking. |
| Dictionaries | `Objects/dictobject.c:ensure_shared_on_read`, `_Py_dict_lookup_threadsafe`, `dict_setdefault_ref_lock_held`, `dict_setdefault_impl`; dummy-slot rule at line 84 | Writers/setdefault use the dict mutex. Optimistic reads load table/value pointers, try strong-reference acquisition and validate table/value-array identity; failure retries under the lock. Shared tables/values are reclaimed later. Deleted dummy slots are not reused in FT tables, supporting safe optimistic lookup. Hash/equality re-entry still complicates atomicity. |
| Biased reference counting | `Include/refcount.h`, `Include/internal/pycore_object.h`, `Objects/object.c`: owner `ob_tid`, local and atomic shared counts, shared-state flags (`MAYBE_WEAKREF`, `QUEUED`, `MERGED`) and merging | Owner-local references avoid an atomic RMW; cross-thread references need atomic accounting and sometimes queued merging. Object death can be delayed and occur on a different accounting path. A positive refcount is not proof of thread confinement; `Py_REFCNT(obj)==1` is not a universal uniqueness test. |
| Deferred references | `Objects/funcobject.c:219` selects top-level functions/methods; `Python/gc_free_threading.c:gc_visit_thread_stacks`, deferred-ref handling; `_PyObject_SetDeferredRefcount` | Selected stack references avoid ordinary counting; collection scans stacks to account for them. This does **not** mean every decref is deferred or every function immortal. A tracing-GC Lucifer can avoid many internal RC updates, but must still define C-visible lifetime and destruction boundaries. |
| Immortalization | `Objects/codeobject.c:should_immortalize_constant`, `Objects/unicodeobject.c` interning; [current 3.14 HOWTO](https://docs.python.org/3.14/howto/free-threading-python.html#immortalization) | 3.13 broadly immortalized functions, modules/dicts, code and types after threads started. 3.14 narrows this to code constants and interned strings, in addition to pre-existing immortal singletons/static objects. The vendored HOWTO's 3.13 list is stale; actual 3.14 function source uses deferred RC instead. Immortality trades retention for less refcount contention, not mutable-data synchronization. |
| QSBR | `Python/qsbr.c:_Py_qsbr_advance`, `qsbr_poll_scan`, `_Py_qsbr_attach`, `_Py_qsbr_detach`; `Include/internal/pycore_qsbr.h`; delayed frees in `Objects/obmalloc.c` | Retire old storage at a sequence; reclaim after all relevant online readers pass a quiescent state. Attach uses a sequentially consistent store and the scan a fence; detach release-publishes offline. This protects old reader access, not the logical contents of an entire container. Merely seeing a newer version is not proof that an old allocation is safe to reuse. |
| Mimalloc / delayed reuse | `Objects/obmalloc.c` requires `WITH_MIMALLOC` for `Py_GIL_DISABLED`, provides per-thread heaps and QSBR-related page/delayed-free handling; `Python/gc_free_threading.c` enumerates allocated objects through mimalloc | Allocator layout/reuse rules participate in safe optimistic reference acquisition. Replacing malloc without preserving those rules would invalidate the read algorithm. Lucifer need not adopt this allocator, but needs an equally explicit reclamation/rooting protocol. |
| GC stops | `Python/gc_free_threading.c`, `Python/pystate.c` attach/detach, stop-the-world state | No GIL does not mean no global pauses. Consistent stack/refcount accounting and cycle processing still need stops; finalizers/callbacks must execute where ordinary Python execution is allowed. Detached native execution must not silently retain unregistered movable pointers. |

One source-level warning deserves retention: 3.14.0
`Objects/listobject.c:821,957,973,1059` has TODOs about `_Py_memory_repeat` and
`memmove`/`memcpy` safety with shared lists, referencing
[GH-129069](https://github.com/python/cpython/issues/129069). This is evidence
against claiming that a mutex plus optimistic reads automatically proves all
bulk paths race-free. Resolution in any selected later 3.14 patch must be
checked independently; it is **unverified here**.

### 2.3 Mapping every dependency to the free-threaded implementation

| Dependency from §1 | 3.13t/3.14t answer and remaining limit |
|---|---|
| List append/pop, dict lookup/set/setdefault | Container sections and optimistic strong-reference reads described above. Per-operation protection preserves ordinary exact-built-in use; callbacks, subclasses, compound statements and unsupported raw C access remain outside the simple guarantee. |
| Iteration/mutation | List iteration uses safe item acquisition; dict iterators check size/state and use safe reference acquisition. No general snapshot. The 3.14.0 iteration tests accept duplicate/missing items for shared iterators; their range specialization even permits values beyond the original range if consistent with its step. That test is a record of a gap, not a desirable Lucifer contract (`Lib/test/test_free_threading/test_iteration.py`). |
| `+=` | No additional application transaction is provided. Use an explicit mutex for a shared counter or a documented atomic API. Container locking cannot combine separate loads and stores. |
| Set and deque | `Objects/setobject.c` and `Modules/_collectionsmodule.c` use critical-section wrappers, including two-object set paths. Preserve the specifically promised endpoint operations; arbitrary-iterable/comparison and compound deque operations are not all-or-nothing transactions. |
| Queue | The Python implementation retains explicit Lock/Condition/Semaphore protocols; C SimpleQueue has its own synchronization (`Modules/_queuemodule.c`). This is a close model for Lucifer's Python-over-primitives requirement. Keep Queue and SimpleQueue reentrancy contracts distinct. |
| Attributes / types / class creation | Object/dict synchronization plus type metadata coordination (`Objects/object.c`, `Objects/dictobject.c`, `Objects/typeobject.c`). `TYPE_LOCK` is an interpreter-wide **metadata** mutex; per-object locks alone are not the full design. Caches and MRO/type version invalidation must agree. Class body and hooks remain ordinary interruptible Python. |
| Imports / module initialization | Existing global import-bookkeeping lock and per-module recursive locks remain (`Lib/importlib/_bootstrap.py`, `Python/import.c`). Different module initialization is allowed concurrently; same-module normal import waits. Legacy extension initialization can transiently enable the GIL before support is known (`Python/import.c:_PyEval_EnableGILTransient` call sites). |
| Finalization | Biased merging and deferred references can postpone destruction; immortals do not die. GC handles cycles/resurrection. No general guarantee of GIL-era prompt timing or a single callback thread. Code relying on refcount-driven resource release belongs in the differential corpus, rather than being dismissed as bad style (`Objects/object.c`, `Python/gc_free_threading.c`, `Lib/weakref.py`). |
| Shared generators | **Known gap in the pinned baseline.** `gen_send_ex2` checks and sets `gi_frame_state` without an atomic acquisition; optimized generator paths in `Python/bytecodes.c` also change it. [GH-156351](https://github.com/python/cpython/issues/156351) reports crashes on 3.13t/3.14t. Its [closing maintainer comment](https://github.com/python/cpython/issues/156351#issuecomment-5473993496) says the changes are too large to backport; closure does not establish a fix. Reported reproduction counts were not rerun here. Lucifer should implement safe rejection of overlapping execution, even where this baseline crashes. |
| Trace/profile | `Python/legacy_tracing.c` uses setup synchronization and a stop-the-world update of tracing state, then monitoring instrumentation. `test_monitoring.py` stresses toggling and all-thread profiling. Callback execution on different threads can overlap; arbitrary access to another executing frame is not made safe. |
| Weakref callbacks | `Objects/weakrefobject.c:11` documents a weakref hash object lock and a striped referent-address lock for mutable weak lists/state; no-detach restrictions prevent suspension in sensitive removal paths. References are cleared and callbacks gathered before callbacks run outside the raw lock. Python weak-container algorithms still need their own protocol. |
| Signals | Main-thread/main-interpreter routing stays. Atomic pending flags and wakeup-FD ordering bridge native delivery to Python check points (`Modules/signalmodule.c:trip_signal`). Signals do not become arbitrary-worker Python callbacks. |
| Extension static state | An extension must opt in, then supply its own synchronization for C globals; the interpreter cannot infer it. Without opt-in, CPython can enable a GIL and warn. This fallback is incompatible with Lucifer's no-GIL requirement; see §3.4. |

### 2.4 What `Py_mod_gil` actually promises

Multi-phase extensions declare `{Py_mod_gil, Py_MOD_GIL_NOT_USED}`; single-phase
ones use `PyUnstable_Module_SetGIL`. It is an author declaration, not static
analysis, certification, or automatic locking. The vendored NumPy core
declares it in `numpy/_core/src/multiarray/multiarraymodule.c:5795`. CPython
warns and permanently enables the GIL for an undeclared module in the normal
default configuration; `PYTHON_GIL=0` / `-X gil=0` suppress that safety fallback
at the caller's risk (`Python/import.c:_PyImport_CheckGILForModule`).

The full C-API still contains dangerous interfaces: direct mutable struct
fields, unchecked list macros, borrowed-reference getters followed by incref,
and `PyDict_Next` without an enclosing section. Strong-reference alternatives
include `PyList_GetItemRef`, `PyDict_GetItemRef`, `PyDict_SetDefaultRef` and
`PyWeakref_GetRef`. Attached thread state remains necessary without a GIL;
SaveThread/RestoreThread and ALLOW_THREADS still delimit safe native/GC
participation. All these details are documented in
`vendor/cpython/Doc/howto/free-threading-extensions.rst` and
[the extension HOWTO](https://docs.python.org/3.14/howto/free-threading-extensions.html).

There is also an **ABI** boundary: ordinary CPython and free-threaded builds
have distinct object layouts/binary tags; a `cp314` ordinary wheel is not
automatically a `cp314t` wheel. Runtime GIL fallback applies to extensions built
for the free-threaded ABI that lack a support declaration; it does not convert
an arbitrary old binary. Full C-API source support, binary ABI compatibility,
and thread safety are separate deliverables. The owner-specified unmodified
NumPy build must be named and fingerprinted in the acceptance manifest; this
report does not silently replace it with a patched NumPy or a different ABI.

## 3. Lucifer design over the Smalltalk substrate

Everything in this section is a **proposal**, not evidence of existing Lucifer
code. Reuse the Smalltalk ownership, continuation and epoch discipline; add
Python-specific semantics at the operations that require them.

### 3.1 What the Smalltalk documents actually supply

The following paths name blobs read through `origin/master`, not the Smalltalk
working tree. The system map is living documentation; historical design
sections sometimes describe mechanisms superseded by later sections.

| Verified substrate | Exact document / invariant | Constraint on the Python design |
|---|---|---|
| Single execution owner, eligibility authority and cooperative epochs | `docs/04-SYSTEM-MAP.md` park/resume and poll-divert walks; `docs/05-INVARIANTS.md` **INV-SCH-1,11,12** | Producers post readiness; only scheduler admission derives execution eligibility. A requested stop is not an acknowledged stop. Reclamation waits for executing owners and outstanding native work. |
| Published continuations and precise roots | `docs/05-INVARIANTS.md` **INV-EXE-1**, **INV-GC-1** | Before park/stop/inspection, publish exact frame geometry, PC and live references, including SSA/register/spill state via descriptors. Python frames, trace state and exception state need the same treatment. |
| Exact wait identity, retained result, execution lease | `docs/vm/wait-DESIGN.md` | Match a completion to a wait ID, retain early results, reject stale/duplicate completions, and apply results only with the target continuation's lease. Use this for Python conditions, futures, I/O and cancellation races. |
| Worker-local allocation; globally coordinated collection | `docs/vm/gc-DESIGN.md` | Each allocation context bumps its own nursery, but young collection covers all contexts because migrating processes create cross-nursery edges. Do not infer per-task isolation from where an object was allocated. |
| One store barrier for both execution engines | `docs/vm/gc-DESIGN.md:gc_field_store`; **INV-GC-20** in `docs/05-INVARIANTS.md` | A raw field store and remembered-set tracking maintain GC reachability; they are **not** a Python synchronization or strong-reference-acquisition protocol. Shared stores also need publication ordering and lifetime protection. |
| Fairness is distinct from safepoint safety | `docs/vm/fairness-DESIGN.md`, production selection | CPU loops must reach polls; fairness hints request canonical yield, with execution-lease accounting and a retunable 5 ms initial producer period. This is not a hard latency bound and cannot interrupt an arbitrary native call. |
| Contention was measured and revised | `docs/vm/scheduler-concurrency-DESIGN.md`, especially §§4,6,7; `docs/04-SYSTEM-MAP.md` | Later results use a futex-backed adaptive mutex and worker-local frame reuse; the current map describes worker-local run queues. Do not copy the design record's opening historical central-FIFO description as current fact, or put every Python append under `g_sched_lock`. |
| Worker exit / foreign work lifetime | **INV-SCH-10**; `docs/vm/worker-stack-cleanup-DESIGN.md` | OS clear-TID join precedes stack unmapping. A Python Thread's logical completion, extension callback completion and native-thread retirement are distinct lifetime events. |

### 3.2 Thread, event loop and lightweight task mapping

| Python surface | Proposed mapping | Compatibility conditions |
|---|---|---|
| `threading.Thread` / `_thread` | A VM Process with a Python thread-state record and, initially, a dedicated OS carrier bound for its lifetime; register it in the same stop/root/foreign-call protocols as M:N workers | This conservative mapping preserves `native_id`, OS TLS, C/C++ `thread_local`, pthread ownership and native callback affinity. Python-level join, locks, exception hooks and thread-local storage can remain Python over native wait/identity primitives. Pinning only at the instant of a C call is insufficient: an extension can retain OS TLS across calls. |
| `asyncio` | One event loop belongs to one Python Thread; its callbacks/tasks execute serially on that loop's Process/carrier. Awaited I/O parks/wakes using the reactor/wait protocol. Multiple loops on different Threads can run in parallel | Keep run-to-await/run-to-callback-return behavior and FIFO ready-queue semantics. Do not turn every asyncio Task into a concurrently running process. `call_soon_threadsafe` and `run_coroutine_threadsafe` are cross-loop ingress; cancellation stays cooperative. `base_events.py:_check_thread`, `_run_once`, `call_soon_threadsafe` are the compatibility anchors. |
| New explicit lightweight parallel tasks | M:N Processes on the general worker pool, separate from the `threading.Thread` compatibility surface | Task identity owns Python execution context, exception state and task-local data. ContextVars are copied at spawn under an explicit API contract. A task can migrate at canonical polls; unrestricted foreign calls require a supported affinity policy or a permanently bound carrier. No automatic promise that arbitrary native TLS is task-local. |
| Blocking native calls and foreign-created threads | Safe detached native work uses a bounded blocking pool/carrier, with retained roots/pins; foreign callbacks attach a thread state before Python entry | A blocked C stack cannot simply migrate or disappear. Native threads and detached work must participate in GC/reclamation. Pooling can compensate for a blocked worker only after bounding oversubscription and preserving identity. `PyGILState_Ensure` becomes attachment, never a global serialization token. |

The first row deliberately does not force all Python Threads into M:N. A fully
migratable `threading.Thread` with transparent arbitrary native TLS and stable
OS thread identity is **unverified** and unsupported by the source read here.
M:N still supplies cheap explicit tasks, asynchronous waits and Python-only
services. A later affinity-elision optimization requires an actual proof that
the thread never observes native identity/TLS, not merely absence of a recent
C call. Evidence motivating this boundary: CPython's
`Lib/threading.py`, `Modules/_threadmodule.c`, `Python/pystate.c`, and the
Smalltalk worker-stack/current-worker assumptions in `gc-DESIGN.md`.

Thread state must own tracing, recursion accounting, exception stacks,
`threading.local`, Python ident and attached/detached status. Worker state owns
nursery, machine stack and worker counters. Task state owns ContextVars and
cancellation. Keep these distinct when a logical task runs on another worker.
Match 3.14t's default copied thread context and context-aware warning filters
(`vendor/cpython/Doc/howto/free-threading-python.rst`, `Lib/threading.py`,
`Lib/warnings.py`); record explicit alternative flags in differential runs.

### 3.3 Cheap list/dict semantics with Python implementations

Implement list/dict algorithms in Python and let the JIT lower a small set of
native synchronization, allocation and reference/lifetime primitives. A
method written in Python does not automatically preserve the atomicity of the
corresponding CPython C implementation: the compiler needs explicit semantic
operation boundaries. Proposed order of implementation:

1. **Correct shared slow path first.** A small per-object mutex; park the
   Process on contention rather than blocking every pool worker. Exact-list
   append/pop and dict set/setdefault use a bounded critical region. Reserve
   storage/prepare fallible work before commit where possible; repeat lookup
   after any unlock. Commit table/size/entry changes consistently, then release
   old owned references/call destructors after structural invariants hold.
   Keep native primitives general (mutex, ordered reference load/store/CAS,
   allocation/retirement, root publication), not one giant native container
   implementation. Precedents: CPython list/dict paths in §2 and
   `Lib/queue.py`.
2. **Elide locking for proven private objects.** JIT escape analysis can omit
   synchronization only while no other Process/Thread/C alias can access the
   object. A thread/worker ownership tag by itself is insufficient. On first
   escape, revoke the private assumption and publish a shared representation
   before another executor can read it. Moving a task between workers must
   not turn a shared object private. Native exposure, borrowed pointers and
   callbacks force a conservative path. This is a proposed optimization,
   motivated by CPython's ownership/share transitions, not an established
   Lucifer performance claim.
3. **Optimistic shared reads.** Enter a bounded read/lifetime interval before
   loading any reclaimable pointer; acquire-load the table/array, load a
   candidate, register a strong root/reference and validate pointer/version
   state. Retry/fallback under the object mutex on interference. Never
   incref/root a possibly freed pointer after merely checking a version.
   Retire replaced arrays/tables after a grace period; forbid address-reuse
   ABA, version wrap and speculative reads of invalid storage. CPython's
   `list_get_item_ref`, `_Py_dict_lookup_threadsafe`, `qsbr.c` and allocator
   integration are the concrete comparison, not a proof of our proposed
   tracing-GC variant.
4. **Bounded mutation commit.** In a no-callback/no-allocation commit region,
   keep scheduler polls and signal/trace/finalizer callbacks out until the
   container is coherent; GC requests wait for this short region. Do not
   hold such a region across resizing work proportional to an unbounded
   input. Slow allocation can park before commit. Hash/equality/descriptors
   require a resumable critical-section protocol with state revalidation,
   rather than a large uninterruptible region. Reuse the published-frame
   discipline of INV-EXE-1.
5. **Exact fast-path guards and deoptimization.** Specialize ordinary integer
   indexes and exact string/int keys, with known non-reentrant equality.
   Guard subclass overrides, mutable type/shape versions and storage strategy.
   Deopt before side effects, or resume after an explicitly recorded commit;
   never replay append, default construction, weakref callbacks or a C call.
   Type-cache coherence must pass `test_type.py`'s ordered-observation tests.
6. **Two-object paths and re-entry.** Establish stable lock ordering for
   exact-built-in bulk operations. On an arbitrary iterable/comparison, call
   Python through a boundary that can suspend and revalidate. Do not claim
   bulk atomicity where CPython's callback boundaries do not provide it.
   Immutable tuples/frozensets and private storage can have simpler paths.

For a tracing GC, keep optimistic candidate pointers in recognized roots
before acknowledging any safepoint; a no-safepoint read interval needs an
explicit bound. A parked task is quiescent only after raw borrowed addresses
are eliminated or retained by a separate read lease. A suspended C extension
may pin memory much longer than a worker quantum. Reclamation participation
therefore cannot be just “worker i has polled”; it must include foreign
attached threads and outstanding native/read leases. Smalltalk
`vm_retire`/`vm_grace_step` and INV-SCH-12 are useful precedents, but they do
not by themselves prove Python borrowed-reference safety.

**Lock-free mutation is not the starting recommendation.** A CAS on list
length does not initialize its slot, coordinate resize or secure returned
references; a dict CAS does not handle reentrant hashing, iteration and
reclamation. Start with private lock elision and optimistic reads; evaluate
owner-biased shared locking only after measuring contention, revocation,
memory and migration cost. The CPython bulk-list TODOs in §2 are a concrete
reason to require a proof for each such optimization.

Other Python-specific mechanisms are also needed: an atomic generator
execution-ownership claim with `ValueError` on overlap; per-module import
locks and an initialization state machine; versioned class/cache publication;
main-thread pending-signal polling; trace-enable deopt/instrumentation epochs;
and a finalization/weakref protocol that separates structural teardown from
arbitrary Python callbacks. Their compatibility anchors and known gaps are
the corresponding rows of §§1–2, not Smalltalk object-store semantics.

### 3.4 Extensions that are not free-threading-safe

**Proposed policy: never silently introduce a runtime-wide GIL.** Preserve full
C-API coverage, but classify extension execution separately from API presence.
CPython's transient initialization GIL and permanent fallback are facts to
replace, not mechanisms to copy under another name (`Python/import.c`).

| Extension category | Proposed treatment | What it does not prove |
|---|---|---|
| Free-threading-capable, ABI-compatible, audited by tests | Native execution with attached thread state, required object critical sections, correct allocation domain, strong-reference/root rules and carrier affinity | `Py_MOD_GIL_NOT_USED` alone is insufficient. NumPy must pass its actual unchanged suite/artifact gate, not just import. |
| Legacy library with a bounded, verified serialization envelope | A **per-library** reentrant admission mechanism may serialize that library's native entries, including init, deallocators, callback re-entry and shared dependent libraries; only admit it after auditing all entry paths and shared Python objects | A lock on the Python-call wrapper does not cover an extension's native background threads, capsule function pointers, buffer callbacks, type slots, shared dependencies or callbacks reached through another extension. This is a conditional accommodation, not a generic solution. |
| Unknown/unsafe library | Fail an unsafe direct import explicitly with the module and reason; optionally offer an explicit process-isolated service for APIs that can serialize their values | Process isolation changes object identity, callbacks, pointers and shared-memory behavior; it is **not** transparent full C-API compatibility and cannot satisfy the NumPy acceptance bar in place of an in-process build. Generic automatic in-process safety is **unverified**. |

The unresolved case is concrete: extension A directly reads a borrowed pointer
from a shared list while another Python thread removes it. Serializing A's
entries leaves the Python mutator running. Similarly, two separately locked
extensions may share one native library's globals. Correct support requires
an audited access/locking envelope, a changed extension, or isolation; a
per-extension lock cannot generally recreate everything the GIL protected.
Unchecked C macros/direct fields also bypass a JIT's Python-level barriers
(`Doc/howto/free-threading-extensions.rst`). This is an open implementation
obligation under the fixed requirements, not a proposal to weaken them.

The C-visible representation must preserve the chosen full ABI, including
stable addresses for live exposed objects, compatible layouts and backing-array
resize/borrowing contracts, allocation-domain rules and deallocation ownership.
Moving internal objects may require pinned promotion or stable native representatives; either
must preserve identity, sharing and borrowed-reference behavior. Feasibility
for **all** non-limited accesses is **unverified** here. A design claiming
full C compatibility cannot treat an ABI wrapper as sufficient when native
code performs inline reads/writes.

## 4. Validation corpus and concrete acceptance plan

This is the principal recommendation. Passing familiar examples or importing
NumPy is inadequate: use upstream suites, targeted shared-object histories,
native race instrumentation and schedule perturbation. All commands below are
**future harness specifications** for an isolated implementation/CI host; none
were executed during this read-only research task. Upstream sources under
`vendor/` remain reference material, never execution roots.

### 4.1 What the ecosystem tracker does and does not establish

`vendor/free-threaded-compatibility/docs/tracking.md` is manually maintained and
tracks projects with active support work, including build tools and some
Python projects needing changes. It explicitly does **not** attempt to list
all pure-Python packages. A blank status is unknown/unmarked, not a verified
failure. A version without a release check is **projected**, not shipped.
Wheel availability is a different metric, linked to
[the wheel tracker](https://hugovk.github.io/free-threaded-wheels/).

All rows were parsed as seven-column Markdown records: **152 projects**,
**125 CI checks**, **125 release checks**, **19 nightly indicators**. Thus 27
rows lack a release check. These are counts of table marks, not independent
upstream certifications, not 152 runtime libraries, and not the percentage of
the ecosystem that is safe. Individual wheel availability, support on each
3.13t/3.14t patch and all 152 upstream CI results are **unverified**. The
complete inventory is in §4.7, including caveats the tracker records.

### 4.2 CPython's own corpus and CI

The pinned `Lib/test/test_free_threading/` contains **22 `test_*.py` files**
(plus `__init__.py`) and **86 syntactically declared `test_*` functions/methods**,
counted with Python AST parsing of text. This is not a count of collected
tests: inheritance, parameterization, decorators and platform skips change
the runtime count. The files are:

| Files | Risk exercised |
|---|---|
| `test_dict`, `test_list`, `test_set`, `test_iteration` | Shared keys, lazy instance dictionaries, replacing `__dict__`, list resize/mutation/iteration, set repr/clear |
| `test_type`, `test_slots`, `test_races`, `test_func_annotations` | Attribute/type cache coherence, slot widths, class changes, closure cells, annotations and warning filters |
| `test_monitoring`, `test_code`, `test_gc`, `test_functools` | Trace/profile/monitoring toggles, code metadata, GC inspection, cache operations and re-entry |
| `test_bisect`, `test_heapq`, `test_str`, `test_grp`, `test_tokenize`, `test_methodcaller` | Algorithms layered on containers, concurrent strings, native group lookup, tokenizers, call helpers |
| `test_enumerate`, `test_reversed`, `test_zip`, `test_itertools_batched` | Shared iterator state and result reuse |

`Lib/test/libregrtest/cmdline.py:336` defines
`--parallel-threads=N`; `single.py:_parallelize_tests` wraps each eligible
unittest case, and `parallel_case.py` makes shallow copies, starts them behind
a barrier and combines their results. Tests marked
`__unittest_thread_unsafe__` stay sequential. **`-jN` is process parallelism;
it is not this threaded mode.** Both modes can coexist, so set process count
explicitly to avoid multiplying load accidentally.

`Lib/test/libregrtest/tsan.py` has a curated threaded corpus and a smaller
parallel corpus (`test_abc`, `test_hashlib`). It explicitly comments out
`test_concurrent_futures` for excessive races, and limits parts of `test_capi`
because of open bugs. Those omissions must remain visible in Lucifer's
coverage ledger. `.github/workflows/build.yml:build-san` contains TSan jobs
with and without free threading; `reusable-san.yml` runs `--tsan -j4` and, for
the FT job, `--tsan-parallel --parallel-threads=4 -j4`, with archived sanitizer
logs and suppression files `Tools/tsan/suppressions.txt` and
`Tools/tsan/suppressions_free_threading.txt`. This verifies CI
configuration, **not** that a particular CI run was green.

Extend beyond the 22 files: the relevant existing stdlib suites include
`test_thread`, `test_threading`, `test_threading_local`, `test_threadsignals`,
`test_queue`, `test_deque`, `test_dict`, `test_list`, `test_set`,
`test_importlib`, `test_generators`, `test_genexps`, `test_coroutines`,
`test_asyncgen`, `test_weakref`, `test_gc`, `test_signal`, `test_sys_settrace`,
`test_monitoring`, `test_logging`, `test_asyncio`, `test_concurrent_futures`,
`test_functools`, `test_enum`, `test_dataclasses`, `test_descr`,
`test_subclassinit`, `test_capi`, `test_ctypes`, `test_sqlite3`, `test_ssl`
and `test_io`. Run their normal supported invocation first; some cannot
sensibly be duplicated in parallel because they mutate process-global state.
Keep native helper-module requirements explicit instead of silently skipping
the C-API tests.

### 4.3 pytest-run-parallel, shipped fixes and why both matter

The [plugin README](https://github.com/Quansight-Labs/pytest-run-parallel/blob/main/README.md)
was checked directly. `--parallel-threads=N --iterations=M` runs **copies of
the same test**, synchronized with a barrier, not unrelated tests concurrently
for speed. Fixtures are generally shared. Direct `tmp_path`/`tmpdir` requests
receive special per-thread/iteration handling; indirect fixture requests can
still share state. `thread_unsafe` tests run sequentially by default;
`--skip-thread-unsafe` omits them. `--forever` is for bounded external
reproduction jobs, not an unattended acceptance gate. `--ignore-gil-enabled`
would hide an invalid no-GIL run and must not be used for this gate.

Repeating a test that creates a fresh object per invocation exercises shared
module/native globals, but may never share the library object being tested.
Add explicit shared-object tests with barriers, ownership transfers and
concurrent mutations. Pytest helpers/fixtures that mutate global state need
careful classification; a harness race is not evidence of a VM race. The
distinction and examples are documented in
`vendor/free-threaded-compatibility/docs/testing.md`.

Packages that shipped changes supply regressions with demonstrated value:

| Package / release evidence | Concrete change or recorded issue | Corpus implication |
|---|---|---|
| NumPy **2.1.0**, `vendor/numpy/doc/source/release/2.1.0-notes.rst:115` | Removed globals, used TLS or added locks; object-array access lost former GIL protection. Local `array_coercion.c` disables the global cache in FT builds | Mandatory unchanged full suite plus `numpy/_core/tests/test_multithreading.py`; cover coercion, ufunc caches, RNG state, dtype init, buffer access, weakref/finalizer interaction and context-local print settings. |
| SciPy **1.15.0**, [release notes](https://docs.scipy.org/doc/scipy-1.15.0/release/1.15.0-notes.html) | Released FT support and many race fixes, including [stats thread-safety PR #22175](https://github.com/scipy/scipy/pull/22175) and module declaration fixes | Run all upstream tests normally and eligible tests with threaded repetition; explicitly exercise stats, sparse/spatial structures and callback-bearing numerical routines under their documented thread-safety limits. |
| Hypothesis **6.135.32**, [changelog](https://hypothesis.readthedocs.io/en/latest/changes.html#v6-135-32) | Remaining recursion-limit warning thread-safety fix; tracker identifies this release | Pin a known thread-capable Hypothesis for randomized histories; include concurrent strategy use and replay/minimization. A property runner can itself race. |
| Cython **3.1.0**, PyO3 **0.23**, pybind11 **2.13**, cffi **2.0.0** | Shipped support per the pinned tracker's linked issues; individual generated binaries still need validation | Exercise downstream extension suites and small native callback/TLS/borrowed-reference fixtures. A generator's support declaration does not certify every user extension. |
| Pillow **11.0.0**, pandas **2.2.3**, h5py **3.16.0**, cryptography **46.0.0**, wrapt **1.17.0**, SQLAlchemy **2.0.45** | Release and CI marks in the pinned tracker; historical Queue/weakref pool problem in [bpo-14976](https://bugs.python.org/issue14976) | Provide image/native buffers, object-heavy tables, native serialization, crypto, descriptor/decorator behavior, connection pools and finalizer-heavy Python. Support marks do not override package-specific sharing restrictions. |

NumPy's own threading docs explicitly permit safe read-only sharing while
warning that concurrent mutation/resizing can crash and that object arrays
need extra care (`vendor/numpy/doc/source/reference/thread_safety.rst`).
SciPy similarly documents stateful-object limits in
[its thread-safety guide](https://docs.scipy.org/doc/scipy/tutorial/thread_safety.html).
The acceptance plan must not invent a guarantee of arbitrary unsynchronized
ndarray mutation that those libraries do not offer. Keep such cases as
separately labeled diagnostics or memory-safety hardening work; do not count
an upstream-unsupported data race as a clean correctness oracle.

h5py's [3.16 release notes](https://docs.h5py.org/en/stable/whatsnew/3.16.html)
confirm its new free-threading declaration/tests. Its
[threading guide](https://docs.h5py.org/en/stable/threads.html) explains that
the `phil` lock still serializes HDF5 calls without a Python GIL; the guide's
separate experimental/wheel paragraph explicitly describes 3.15 and is not a
3.16 wheel inventory. Matplotlib's
[threading FAQ](https://matplotlib.org/stable/users/faq.html#work-with-threads)
requires artist-access synchronization and usually main-thread GUI operation;
an FT support mark does not remove those limits.

### 4.4 Package suite selection and future invocation

Pin exact package/source/test/plugin revisions and unchanged native artifact
hashes at campaign start. The tracker lists **first** supporting releases,
not recommended installation pins; some old releases predate 3.14 entirely.
Choose maintained releases whose own metadata supports the selected 3.14t
baseline, record that resolution, and retain first-release regressions as
separate fixtures. Installed test-suite presence and precise release pins for
packages not vendored here are **unverified**, so source-test packaging must
be resolved in the future harness rather than assumed.

| Gate | Concrete package suites | Run shape and additional cases |
|---|---|---|
| **A: public compatibility gate** | **NumPy's whole upstream test suite against the owner-selected unmodified build**, with its required test data/helpers | Run its documented full suite sequentially in each runtime; no patching NumPy to make Lucifer work. Then use upstream threaded tests and eligible pytest repetition. Exercise numeric, string, structured and object dtypes, linalg/FFT/random, C-API test helpers, errors and cleanup. Record every skipped/xfail node and why. This gate cannot be replaced by selected submodules. |
| **B: first scientific expansion** | SciPy, pandas, scikit-learn, h5py, Pillow, matplotlib | Full serial suites; threaded eligible subset; explicit same-object tests only for documented supported use. For h5py/native library global locks, assert correctness while reporting expected serialization. For matplotlib use headless tests and preserve main-thread/global-state restrictions. |
| **C: native binding and storage** | cffi, wrapt, cryptography, PyNaCl, msgpack, multidict, yarl, frozenlist, propcache, psutil, SQLAlchemy | Full serial suites plus threaded repetition. Add callback-from-foreign-thread, repeated init/teardown, buffer export/release, per-library dependency sharing, connection-pool/weakref finalization and TLS persistence cases. Include representative PyO3/pybind11/Cython-generated extensions, not just their build-tool tests. |
| **D: async/server and pure-Python integration** | aiohttp, uvloop, Tornado where a supporting release exists; pytest, Hypothesis, joblib; stdlib logging/asyncio/futures | Parallel event loops on different Threads, thread-safe loop ingress, task cancellation/TaskGroup cleanup, executor shutdown, logging queues and context propagation. Run unsupported tracker entries as diagnostics only until support is confirmed; Tornado is projected in this snapshot. |
| **E: wider ecosystem / compatibility negatives** | Every remaining tracked runtime package as resources/platforms allow; greenlet and unknown/non-supporting extensions as explicit compatibility probes | Separate GPU/OS-specific shards. Test deterministic refusal or explicit isolation for unsupported direct extensions and verify that no GIL is enabled. Known greenlet 3.14t shutdown caveat must stay visible. Tool projects are packaging/build probes rather than Python object-sharing coverage. |

Use an installed interpreter and copied/installed test corpus outside
`vendor/`. Examples of future invocations (the executable paths are illustrative,
not present or tested in this research):

```sh
# CPython oracle: installed 3.14t with its test package and C test helpers.
python3.14t -X gil=0 -X faulthandler -m test -j1 test_free_threading
python3.14t -X gil=0 -X faulthandler -m test -j1 --parallel-threads=4 test_abc test_hashlib
python3.14t -X gil=0 -X faulthandler -m test -j1 test_queue test_importlib test_weakref test_asyncio test_generators

# NumPy: use its installed full-test entry point and test dependencies.
python3.14t -X gil=0 -m pytest --pyargs numpy -v
python3.14t -X gil=0 -m pytest --pyargs numpy -v --parallel-threads=4 --iterations=20

# Other packages with installed tests; run outside their source checkouts.
python3.14t -X gil=0 -m pytest --pyargs scipy -v
python3.14t -X gil=0 -m pytest --pyargs scipy -v --parallel-threads=4 --iterations=20
```

Substitute the Lucifer executable for the **same** corpus and manifest; do not
invent `-X gil` support for Lucifer merely to copy a command. If an upstream
project requires its own test entry point, use that instead and record the
equivalent selectors. Keep thread-unsafe tests in the serial/full lane; use
`--skip-thread-unsafe` only for an additional threaded/sanitizer lane with
an exported exclusion list. Run with `-v` to distinguish serial PASS from
PARALLEL PASS. Use per-test/subprocess watchdogs; collect all worker exceptions
and hang traces. Never treat swallowed thread exceptions as a successful test.

Every oracle job must check `sysconfig.get_config_var("Py_GIL_DISABLED") == 1`
and `sys._is_gil_enabled() is False` after importing all test plugins and
target extensions, and again at completion. Separate automatic-fallback
diagnostics run without forced GIL-off to discover which imports trigger it;
those runs cannot qualify as no-GIL passes. Force-off does not make an unsafe
extension safe: its support status still controls admission. Lucifer needs
equivalent runtime assertions and two simultaneously executing Python workers
as a sanity test, rather than merely accepting an option named “no GIL”.

### 4.5 Shared-object histories, stress and differential rules

Each row below becomes a small model-based regression family, independent of
pytest fixture sharing. Log operation invocation/return, unique value IDs,
thread/task identity and a reproducible schedule seed. For guaranteed atomic
operations, check a sequential history respecting non-overlapping real-time
order, including return values/errors; checking only final container length
misses invalid reads, duplicate pops and publication errors.

| Family | Required scenarios and oracle |
|---|---|
| Lists/deques | Many producers append unique tokens; consumers pop until completion is signaled by a proper synchronization primitive. Every token is removed exactly once, no fabricated values, no lost inserts. Race read/pop/resize/reverse/slicing with legal-result checks; separately test list-mutation iteration's permitted outcomes and shallow exact-built-in snapshots. Bounded deque eviction must leave structure coherent before finalizers re-enter. |
| Dicts/sets | Distinct-key writes, same-key contention, delete/reinsert/resize, setdefault winner **identity**, colliding keys and recursive hash/equality, two-container operations with opposite argument order. For setdefault, expect one installed object and coherent returns, not a single default-constructor execution. Model memory-safe arbitrary-key callbacks separately from exact non-reentrant keys. |
| Negative atomicity controls | Unlocked `+=` and check-then-act examples must not be used as correctness assertions. Create a deterministic hook/barrier between read and write to demonstrate the permitted lost-update history, then assert lossless results for the locked version. A counter that happens to reach its total under one schedule proves nothing. |
| Publication / JIT | Initialize an object, publish through Queue/Event/Lock, read all fields on another thread. Repeat after tier promotion, deopt, strategy changes, type/descriptor replacement and task migration. Check type-dict versus cached-read ordering from `test_type.py`; force collection and address reuse while readers hold roots. |
| Import / class construction | Concurrent same-name import with observable once-per-successful-load execution; different-name overlap; failure/retry, recursive/circular imports, native init callbacks, class factories sharing bases, `__set_name__`/`__init_subclass__` publication, subclass-list iteration and cache invalidation. Preserve circular partial-init behavior rather than requiring all imports to see complete modules. |
| Lifetime / weakrefs | Acyclic last-reference release, cross-thread handoff/merging, cycles, resurrection, weakref acquire versus last release, callbacks that add/remove weak entries, logging or queueing during destruction, shutdown. Assert no resurrection through stale pointers, no double callback/deallocation, and documented callback ordering per referent. Record prompt timing and callback-thread differences explicitly. |
| Generators / tracing / signals | Sequential generator handoff must work; overlapping next/send/throw/close must reject safely and never execute the frame twice. Toggle trace/profile/monitoring while code crosses tiers; ensure per-thread event attribution. Deliver native signals to workers while the main thread waits; Python handler runs only in the permitted main context, with flag-before-wakeup behavior. |
| M:N / blocking / native ABI | Runnable tasks greatly outnumber workers; test saturated CPU loops plus a short task, lock contention, timer fairness, blocking native calls and independent I/O progress. Native TLS survives repeated calls on the same Python Thread. Callback attachment, nested waits, stale completion/cancellation, and GC during detached work retain valid roots. |

**Proposed finite stress budget.** On each supported platform run worker counts
**1, 2, 4, 8** (cap at available cores for the regular lane), with task counts
**1× and 16× workers** in M:N-specific tests. Per change: 20 repetitions of
each targeted history under 10 seeds; nightly: 100 seeds with bounded
timeouts. These are starting coverage budgets, not measured sufficiency.
Use separate low-resource settings for expensive upstream suites and
instrumented jobs; do not multiply upstream internal 500-thread tests by
outer thread replication accidentally (`numpy/_core/tests/test_multithreading.py`
already has such cases and marks itself thread-unsafe to outer replication).

**Proposed perturbation points.** Seeded yields/delays around mutex
acquire/release, before/after publication and reference acquisition, table
resize/retirement/reuse, critical-section suspension/resumption, thread-state
attach/detach, callback entry/exit, cancellation/completion and JIT deopt.
Request GC and trace changes at canonical safepoints; make allocator churn
reuse retired addresses after legal grace periods. Never insert a semantic
yield in the middle of an operation promised atomic and then label the
result a valid schedule. Use a separate fault-injection mode to test illegal
transition detection. Smalltalk's `--stress <seed>` entry in
`docs/04-SYSTEM-MAP.md` is a useful precedent, not an existing Lucifer flag.

**Differential matrix.** Run identical package releases/test sources on CPython
**3.14.0t** for the pinned-source reference, a pinned maintained **3.14.x t**
for later fixes, ordinary **3.14.x** as a GIL-era behavior control, and Lucifer
with optimization off/on and deliberate tier/deopt transitions. Use the exact
unchanged native artifact for Lucifer and its declared ABI-compatible CPython
target; ordinary and FT CPython controls need distinct ABI-matched binaries,
whose hashes must both be recorded. Include 3.13t when a release-specific
regression requires it. Match ContextVars,
warning flags, hash seeds and external native-library thread limits. Compare
values, exceptions, identity relations and protocol order; compare sets of
permitted concurrent histories rather than exact timing or a single output
ordering. Keep deterministic package outputs/serial behavior exact, except
documented platform/numeric tolerances.

For a CPython crash or known bug, preserve the reproducer and exact reference
failure in an upstream-bug ledger. Require Lucifer to meet the stated safety
contract; do not imitate the crash or count a shared failure as a pass. The
generator crash and permissive range-iterator tests in §2 demonstrate why an
oracle cannot simply be “whatever this 3.14t run did”. A new discrepancy
requires minimization and classification as VM bug, package bug, harness
race, unsupported sharing or intentional/versioned semantic difference.

### 4.6 Native race detection and the meaning of passing

Use CPython's TSan corpus and instrumented native package builds as a
complement, on a future CI host. Keep the owner-selected **unmodified NumPy
artifact lane** separate from a sanitizer build: recompiling with instrumentation
changes the artifact even when sources are unchanged. The vendored NumPy
`.github/workflows/compiler_sanitizers.yml` has TSan setup and selects tests
by threading imports; `_core/tests/test_multithreading.py` even identifies
cases with no observable assertion that only TSan can detect. A text filter
misses indirect/native thread creation, so it is not a complete suite selector.

Follow `vendor/free-threaded-compatibility/docs/thread_sanitizer.md`: preserve
native logs outside pytest capture (`-s` or `log_path`), time out hangs, and
instrument runtime plus extensions/dependencies consistently. A representative
future configuration is `TSAN_OPTIONS='halt_on_error=1 allocator_may_return_null=1'`.
Maintain a small, reviewed suppression list linked to upstream issues; never
suppress all races from an extension. C-API helpers, dependent native
libraries and the exact compiler/TSan version belong in the manifest.

**Assembly/JIT limitation:** TSan uses compiler instrumentation and a runtime
([Clang documentation](https://clang.llvm.org/docs/ThreadSanitizer.html)).
It therefore does not automatically instrument fasm or generated machine code.
A clean report from
instrumented C extensions cannot certify Lucifer's JIT stores, mutexes,
barriers or scheduler switches. Research runtime/JIT access instrumentation
and sanitizer happens-before/fiber annotations, plus model-based histories
and internal ownership assertions. Correct annotation/instrumentation for
this VM is **unverified**, and missing coverage must appear in results rather
than being called TSan-clean.

Proposed release acceptance requires all of the following:

1. NumPy's unchanged full-suite/artifact gate passes; all selected package and
   stdlib serial suites pass with explicit collected/run/skip/xfail counts.
   No new Lucifer-specific skips or weakened assertions may silently reduce
   the public bar. Upstream expected failures remain separately identified.
2. Every promised operation's shared-object history meets its model; synchronized
   publication and callback/lifetime tests pass across tiers, GC and scheduler
   modes. Memory-safe rejection is required for overlapping generator execution.
3. Every designated threaded test actually ran with the requested concurrency;
   export serial fallbacks and exclusions. No hidden GIL fallback, swallowed
   worker exception, crash, deadlock, lost wakeup or unbounded starvation.
4. Instrumented native lanes have zero unexplained race reports within their
   recorded coverage, with explicit assembly/JIT blind spots. A clean TSan run
   is necessary evidence where available, not proof of absence of races.
5. Reference failures and semantic differences are minimized and reviewed.
   Unsupported ndarray races, pytest-fixture races and CPython bugs are not
   conflated with passing supported-use tests.
6. Artifacts include runtime/package/compiler revisions, ABI tags and hashes,
   GIL-state checks, platform/cores/worker limits, seed/repetition counts,
   mode/tier/GC settings, operation histories, test-node inventories, timeouts,
   logs and triage disposition. Finite stress passing means those executions
   found no violation; it does not prove all schedules safe.

Performance is a separate evidence track after correctness: one-thread cost,
private-object versus contended-object scaling, application throughput and
tail latency, GC pauses, peak/live memory and native oversubscription. The
owner's “every speed claim across a broad corpus” rule in `README.md` applies;
no benchmark claim or performance acceptance threshold is established by this
research session.

### 4.7 Complete pinned tracker inventory

Source for **every row**: the seven-column table in
`vendor/free-threaded-compatibility/docs/tracking.md` at the revision in §0
([permalink](https://github.com/Quansight-Labs/free-threaded-compatibility/blob/53b7eaf3f5fa312e3deef3b944af35074854af1e/docs/tracking.md)).
Names link to the tracker-provided upstream issue where available. **Y** is
an explicit checkmark/nightly indicator; **—** is unmarked, not “unsupported”.
“First” is the tracker's value and is **projected when Release is —**. All
statuses are this snapshot's claims; current per-project correctness is
**unverified**.

| Project | CI | Release | First | Nightly |
|---|---|---|---|---|
| [aiohttp](https://github.com/aio-libs/aiohttp/issues/8796) | Y | Y | 3.13.0 | — |
| [asv](https://github.com/airspeed-velocity/asv/issues/1515) | — | Y | 0.6.6 | — |
| [Bazel (rules-python)](https://github.com/bazel-contrib/rules_python/issues/2386) | Y | Y | 0.39.0 | — |
| [argon2-cffi-bindings](https://github.com/hynek/argon2-cffi-bindings/issues/67) | Y | Y | 25.1.0 | — |
| [Boost.Python](https://github.com/boostorg/python/issues/503) | Y | — | 1.91.0 | — |
| [bcrypt](https://github.com/pyca/bcrypt/issues/913) | Y | Y | 4.3.0 | — |
| [blis](https://github.com/explosion/cython-blis/issues/124) | Y | — | — | Y |
| [bottleneck](https://github.com/pydata/bottleneck/pull/468) | Y | Y | 1.5.0 | — |
| [brotli](https://github.com/google/brotli/issues/1241) | Y | — | — | — |
| [brotlicffi](https://github.com/python-hyper/brotlicffi/pull/216) | Y | Y | 1.2.0.1 | — |
| [cffi](https://github.com/python-cffi/cffi/issues/126) | Y | Y | 2.0.0 | — |
| [charset-normalizer](https://github.com/jawah/charset_normalizer/issues/616) | Y | Y | 3.4.6 | — |
| [cibuildwheel](https://github.com/pypa/cibuildwheel/issues/1657) | Y | Y | 2.19 | — |
| CMake | — | Y | 3.30.0 | — |
| [conda](https://github.com/conda/conda/issues/14053) | Y | Y | 24.11.0 | — |
| [connector-x](https://github.com/sfu-db/connector-x/issues/916) | Y | Y | 0.4.6 | — |
| [ContourPy](https://github.com/contourpy/contourpy/issues/407) | Y | Y | 1.3.0 | Y |
| [cramjam](https://github.com/milesgranger/cramjam/issues/199) | Y | Y | 2.11.0 | — |
| [cryptography](https://github.com/pyca/cryptography/issues/12489) | Y | Y | 46.0.0 | — |
| [cuda-python](https://github.com/NVIDIA/cuda-python/issues/350) | Y | — | — | — |
| cuda-tile | — | Y | 1.4.0 | — |
| [cupy](https://github.com/cupy/cupy/issues/8998) | Y | Y | 14.1.0 | — |
| [CuTe DSL](https://github.com/NVIDIA/cutlass/issues/3109) | Y | Y | 4.5.2 | — |
| [cymem](https://github.com/explosion/cymem/pull/53) | Y | Y | 2.0.13 | — |
| [Cython](https://github.com/cython/cython/issues/6221) | Y | Y | 3.1.0 | Y |
| [datafusion](https://github.com/apache/datafusion-python/issues/1324) | Y | Y | 54.0.0 | — |
| [fastrlock](https://github.com/scoder/fastrlock/issues/19) | — | — | — | — |
| [frozenlist](https://github.com/aio-libs/frozenlist/pull/618) | Y | Y | 1.6.0 | — |
| [greenlet](https://github.com/python-greenlet/greenlet/pull/511) | Y | Y | 3.5.2 | — |
| [grpcio](https://github.com/grpc/grpc/issues/38762) | — | — | — | — |
| [hatch](https://github.com/pypa/hatch/issues/2082) | Y | Y | 1.16.3 | — |
| [h5py](https://github.com/h5py/h5py/issues/2475) | Y | Y | 3.16.0 | — |
| [hf-xet](https://github.com/huggingface/xet-core/issues/396) | Y | Y | 1.2.0 | — |
| [httptools](https://github.com/MagicStack/httptools/issues/118) | Y | Y | 0.8.0 | — |
| [hypothesis](https://github.com/HypothesisWorks/hypothesis/issues/4451) | Y | Y | 6.135.32 | — |
| [JAX](https://github.com/jax-ml/jax/issues/23073) | Y | Y | 0.5.1 | Y |
| [joblib](https://github.com/joblib/joblib/issues/1592) | Y | Y | 1.4.2 | — |
| [jupyter_client](https://github.com/jupyter/jupyter_client/issues/1077) | disabled¹ | Y | 8.7.0 | — |
| [jupyterlab](https://github.com/jupyterlab/jupyterlab/issues/16915) | disabled¹ | Y | 4.6.0 | — |
| [kiwisolver](https://github.com/nucleic/kiwi/pull/190) | Y | Y | 1.4.8 | — |
| [kornia-rs](https://github.com/kornia/kornia-rs/issues/226) | Y | Y | 0.1.9 | — |
| [LibCST](https://github.com/Instagram/LibCST/issues/1242) | Y | Y | 1.8.0 | Y |
| [llguidance](https://github.com/guidance-ai/llguidance/issues/256) | Y | Y | 1.6.0 | — |
| [llvmlite](https://github.com/numba/llvmlite/pull/1351) | Y | Y | 0.47.0 | — |
| [lxml](https://bugs.launchpad.net/lxml/+bug/2111289) | Y | — | 7.0.0 | — |
| [lz4](https://github.com/python-lz4/python-lz4/issues/292) | Y | Y | 4.4.5 | — |
| [mamba](https://github.com/mamba-org/mamba/pull/3579) | Y | Y | 2.3.2 | — |
| [marisa-trie](https://github.com/pytries/marisa-trie/pull/128) | Y | Y | 1.3.0 | — |
| [markupsafe](https://github.com/pallets/markupsafe/issues/460) | Y | Y | 3.0.0 | — |
| [matplotlib](https://github.com/matplotlib/matplotlib/issues/28611) | Y | Y | 3.9.0 | Y |
| [maturin](https://github.com/PyO3/maturin/issues/2315) | Y | Y | 1.7.5 | — |
| [Meson](https://github.com/mesonbuild/meson/issues/13263) | — | Y | 1.5.0 | — |
| [meson-python](https://github.com/mesonbuild/meson-python/issues/499) | Y | Y | 0.16.0 | — |
| [ml-dtypes](https://github.com/jax-ml/ml_dtypes/issues/228) | Y | Y | 0.5.1 | — |
| [mlir-python](https://github.com/llvm/llvm-project/issues/105522) | Y | Y | 20.1.0 | — |
| [mlx](https://github.com/ml-explore/mlx/issues/2807) | — | Y | 0.32.1 | — |
| [multidict](https://github.com/aio-libs/multidict/issues/1014) | Y | Y | 6.2.0 | — |
| [msgpack](https://github.com/msgpack/msgpack-python/issues/613) | Y | Y | 1.2.0 | — |
| [msgspec](https://github.com/jcrist/msgspec/issues/744) | Y | Y | 0.20.0 | — |
| [murmurhash](https://github.com/explosion/murmurhash/pull/47) | Y | Y | 1.0.15 | — |
| [mypyc](https://github.com/mypyc/mypyc/issues/1038) | Y | Y | 1.20.0 | — |
| [nanobind](https://github.com/wjakob/nanobind/pull/695) | Y | Y | 2.2.0 | — |
| [ndindex](https://github.com/Quansight-Labs/ndindex/issues/198) | Y | Y | 1.10.0 | — |
| [netcdf4](https://github.com/Unidata/netcdf4-python/issues/1454) | Y | Y | 1.7.4 | — |
| [nox](https://github.com/wntrblm/nox/issues/949) | Y | Y | 2025.10.14 | — |
| [Numba](https://github.com/numba/numba/issues/9928) | Y | Y | 0.65.0 | — |
| [Nuitka](https://github.com/Nuitka/Nuitka/issues/3062) | — | — | — | — |
| [numcodecs](https://github.com/zarr-developers/numcodecs/pull/858) | Y | Y | 0.17.0 | — |
| [numexpr](https://github.com/pydata/numexpr/issues/503) | Y | Y | 2.11.0 | — |
| [NumPy](https://github.com/numpy/numpy/issues/26157) | Y | Y | 2.1.0 | Y |
| nvidia-cudnn-frontend | Y | Y | 1.23.0 | — |
| nvImageCodec | Y | Y | 0.4.0 | — |
| [ONNX](https://github.com/onnx/onnx/issues/6594) | Y | Y | 1.18.0 | — |
| [OpenCV](https://github.com/opencv/opencv-python/issues/1029) | — | — | — | — |
| [openai-harmony](https://github.com/openai/harmony/issues/87) | — | — | — | — |
| [orjson](https://github.com/ijl/orjson#readme) | — | — | — | — |
| [outlines-core](https://github.com/dottxt-ai/outlines-core/issues/248) | Y | — | — | — |
| [packaging](https://github.com/pypa/packaging/issues/727) | Y | Y | 24.0 | — |
| [pandas](https://github.com/pandas-dev/pandas/issues/59057) | Y | Y | 2.2.3 | Y |
| [Pillow](https://github.com/python-pillow/Pillow/issues/8199) | Y | Y | 11.0.0 | Y |
| [pip](https://github.com/pypa/pip/issues/12634) | Y | Y | 24.1 | — |
| [Pixi](https://github.com/prefix-dev/pixi/issues/2732) | — | Y | 0.39.5 | — |
| [polars](https://github.com/pola-rs/polars/issues/27955) | — | — | — | — |
| [preshed](https://github.com/explosion/preshed/pull/54) | Y | Y | 3.0.11 | — |
| [propcache](https://github.com/aio-libs/propcache/pull/84) | Y | Y | 0.3.0 | — |
| [protobuf](https://github.com/protocolbuffers/protobuf/issues/23158) | — | — | — | — |
| [psutil](https://github.com/giampaolo/psutil/issues/2565) | Y | Y | 7.1.2 | — |
| [psycopg](https://github.com/psycopg/psycopg/issues/1095) | Y | — | — | — |
| [py-radix](https://github.com/mjschultz/py-radix/issues/74) | Y | Y | 1.1.0 | — |
| [PyArrow](https://github.com/apache/arrow/issues/43536) | Y | Y | 18.0.0 | Y |
| [PyAV](https://github.com/PyAV-Org/PyAV/issues/1956) | Y | Y | 15.1.0 | — |
| [pybind11](https://github.com/pybind/pybind11/issues/5112) | Y | Y | 2.13 | — |
| [pycares](https://github.com/saghul/pycares/issues/213) | Y | Y | 4.11.0 | — |
| [pycocotools](https://github.com/ppwwyyxx/cocoapi/pull/27) | Y | Y | 2.0.10 | — |
| [pycryptodome](https://github.com/Legrandin/pycryptodome/issues/813) | Y | Y | 3.23.0 | — |
| [pydantic](https://github.com/pydantic/pydantic/issues/10483) | Y | Y | 2.11.0 | — |
| [pydantic-core](https://github.com/pydantic/pydantic-core/issues/1555) | Y | Y | 2.29.0 | — |
| [pygit2](https://github.com/libgit2/pygit2/issues/1367) | Y | Y | 1.19.0 | — |
| [PyNaCl](https://github.com/pyca/pynacl/issues/857) | Y | Y | 1.6.0 | — |
| [PyO3](https://github.com/PyO3/pyo3/issues/4265) | Y | Y | 0.23 | — |
| [PyObjC](https://github.com/ronaldoussoren/pyobjc/issues/608) | Y | Y | 11.0 | — |
| [PySide6](https://github.com/ctismer/pyside6-freethreading) | — | — | 6.12.0 | — |
| [PyTables](https://github.com/PyTables/PyTables/issues/1269) | Y | Y | 3.11.0 | Y |
| [Pythran](https://github.com/serge-sans-paille/pythran/issues/2258) | Y | Y | 0.18.0 | — |
| [PyTorch](https://github.com/pytorch/pytorch/issues/130249) | Y | Y | 2.6.0 | Y |
| PyWavelets | Y | Y | 1.7.0 | Y |
| [pywinpty](https://github.com/andfoy/pywinpty/issues/491) | Y | Y | 2.0.15 | — |
| [PyYAML](https://github.com/yaml/pyyaml/issues/856) | — | Y | 6.0.3 | Y |
| [PyZMQ](https://github.com/zeromq/pyzmq/issues/2067) | Y | Y | 27.0.0 | — |
| [QuantLib](https://github.com/lballabio/QuantLib-SWIG/pull/734) | Y | Y | 1.42 | — |
| [RapidFuzz](https://github.com/rapidfuzz/RapidFuzz/issues/399) | Y | Y | 3.14.3 | — |
| [rpds-py](https://github.com/crate-py/rpds/issues/101) | Y | Y | 0.22.3 | — |
| [ruamel.yaml.clib](https://sourceforge.net/p/ruamel-yaml-clib/tickets/44/) | — | — | — | — |
| [rust-numpy](https://github.com/PyO3/rust-numpy/issues/476) | Y | Y | 0.24.0 | — |
| [safetensors](https://github.com/huggingface/safetensors/issues/572) | Y | — | 0.9.0 | — |
| scikit-build-core | Y | Y | 0.9.5 | — |
| [scikit-image](https://github.com/scikit-image/scikit-image/issues/7464) | Y | Y | 0.25.2 | Y |
| [scikit-learn](https://github.com/scikit-learn/scikit-learn/issues/28978) | Y | Y | 1.6.0 | Y |
| [SciPy](https://github.com/scipy/scipy/issues/20669) | Y | Y | 1.15.0 | Y |
| [sentencepiece](https://github.com/google/sentencepiece/issues/1110) | Y | Y | 0.2.1 | — |
| [setproctitle](https://github.com/dvarrazzo/py-setproctitle/issues/147) | Y | Y | 1.3.6 | Y |
| setuptools | Y | Y | 69.5.0 | — |
| [setuptools-rust](https://github.com/PyO3/setuptools-rust/pull/502) | Y | Y | 1.11.0 | — |
| [SGLang](https://github.com/sgl-project/sglang/issues/22889) | — | — | — | — |
| [Shapely](https://github.com/shapely/shapely/pull/2138) | Y | Y | 2.1.0 | Y |
| [soundfile](https://github.com/bastibe/python-soundfile/issues/467) | Y | Y | 0.14.0 | — |
| [spacy](https://github.com/explosion/spaCy/discussions/13847) | — | — | — | — |
| [Spack](https://github.com/spack/spack-packages/pull/2632) | Y | Y | 2026.03.0 | — |
| [sqlalchemy](https://github.com/sqlalchemy/sqlalchemy/issues/12881) | Y | Y | 2.0.45 | — |
| [srsly](https://github.com/explosion/srsly/issues/116) | — | Y | 2.5.2 | — |
| [statsmodels](https://github.com/statsmodels/statsmodels/issues/9707) | Y | Y | 0.15.0 | — |
| [StringZilla](https://github.com/ashvardanian/StringZilla/issues/296) | Y | Y | 5.0.4 | — |
| [SWIG](https://github.com/swig/swig/issues/3121) | Y | Y | 4.4.0 | — |
| [thrift](https://issues.apache.org/jira/browse/THRIFT-5888) | — | Y | 0.23.0 | — |
| [tiktoken](https://github.com/openai/tiktoken/issues/406) | Y | Y | 0.12.0 | — |
| [tokenizers](https://github.com/huggingface/tokenizers/issues/1784) | Y | — | 1.0.0 | — |
| [torchao](https://github.com/pytorch/ao/issues/3243) | — | — | — | — |
| [torchgeo](https://github.com/torchgeo/torchgeo/pull/3319/) | Y | Y | 0.9.0 | — |
| [tornado](https://github.com/tornadoweb/tornado/issues/3501) | Y | — | 6.6 | — |
| [tox](https://github.com/tox-dev/tox/issues/3391) | Y | Y | 4.26.0 | — |
| [Triton](https://github.com/triton-lang/triton/issues/8491) | — | — | — | — |
| [uv](https://github.com/astral-sh/uv/issues/7193) | Y | Y | 0.4.24 | — |
| [uvloop](https://github.com/MagicStack/uvloop/pull/693) | Y | Y | 0.22.1 | — |
| [ultrajson](https://github.com/ultrajson/ultrajson/issues/684) | Y | Y | 5.13.0 | — |
| [vLLM](https://github.com/vllm-project/vllm/issues/28762) | — | — | — | — |
| [wrapt](https://github.com/GrahamDumpleton/wrapt/pull/269) | Y | Y | 1.17.0 | — |
| [xformers](https://github.com/facebookresearch/xformers/issues/1345) | Y | Y | 0.0.35 | — |
| [xgrammar](https://github.com/mlc-ai/xgrammar/issues/500) | Y | Y | 0.1.31 | — |
| [xxhash](https://github.com/ifduyue/python-xxhash/issues/101) | Y | Y | 3.6.0 | — |
| [yappi](https://github.com/sumerc/yappi/issues/179) | — | — | — | — |
| [yarl](https://github.com/aio-libs/yarl/issues/1455) | Y | Y | 1.20.0 | — |
| [zstandard](../../vendor/free-threaded-compatibility/docs/tracking.md) | Y | — | — | — |

¹ CI disabled/flaky as explained below.

Tracker footnotes that change interpretation:

- Bazel's release is in the Bazel Central Registry, not necessarily PyPI.
- CuTe DSL support uses `nvidia-cutlass-dsl-libs-base` and
  `nvidia-cutlass-dsl-libs-cu13`.
- **greenlet:** fully supported on 3.15t and above; **3.14t may crash at
  runtime exit**, according to the tracker. A release check is not a clean
  3.14t guarantee.
- **jupyter_client / jupyterlab:** described as functional, with flaky CI
  disabled, blocked on msgpack and on jupyter_client/msgpack respectively.
- zstandard's note points to 3.14's `compression.zstd` and its backport; this
  is not a release check for the tracked extension.
- The source also explains non-PyPI distribution for PyO3 (crates.io) and
  SWIG (website/tag). Preserve these distinctions when resolving artifacts.

## 5. Recommendation and open decisions

**Recommendation (proposal):** establish the semantics and corpus above as
the compatibility contract, implement Python built-ins over explicit native
synchronization/lifetime primitives, start shared mutations with per-object
critical sections, and optimize proven-private accesses and shared reads
first. Reuse Smalltalk's canonical continuations, exact waits, root authority
and epoch ownership; preserve real Python thread/native affinity. Keep the
NumPy unmodified-build gate mandatory and early. The concrete precedents are
CPython's list/dict/QSBR implementation, `Lib/queue.py`, the Smalltalk invariant
register and the upstream tests cited in §§1–4.

| Open decision | Proposed default / experiment | Evidence needed to close it |
|---|---|---|
| Exact Python/C ABI and NumPy acceptance artifact | Name full Python version/ABI, package build and hash explicitly; preserve the owner's unchanged-build requirement | Demonstrate direct/inlined C-API accesses, object layout, buffers, refcounts, identity and cleanup against that artifact; ordinary-versus-`t` wheel support cannot be left implicit. |
| Python memory model | Publish per-operation guarantees and synchronization ordering; callback-sensitive operations get explicit limits | Litmus tests and model-checked histories including cache invalidation; no reliance on a CPython bytecode boundary. |
| Thread versus task identity | Bound OS carriers for compatibility Threads; M:N for explicit lightweight tasks | Native TLS/native_id/foreign-callback tests before allowing thread migration. Measure carrier cost separately from task cost. |
| Private-owner lock elision and shared-state transition | Start with compiler-proven confinement; conservative sharing on escape/C exposure | Revocation/publication proof, migration/deopt/GC stress and measured allocation/header cost. No borrowed alias may outlive the proof. |
| Optimistic reads / reclamation | Locked fallback plus explicit lifetime/read leases and deferred storage retirement | Prove roots, ABA/reuse, epoch wrap, stalled native readers and callback suspension; measure retained memory and long grace periods. |
| Refcount-visible timing and finalizers | Preserve ordinary observable behavior where practical; distinguish prompt acyclic release from deferred/cyclic collection | Run resource-pool/logging/weakref regressions; decide compatibility policy for callback thread, shutdown and promptness. Tracing GC alone does not settle this. |
| Unsafe extensions | Audited direct support or bounded per-library accommodation; explicit failure/isolation for unknown cases | Inventory every native entry path and shared dependency. General transparent safety for arbitrary legacy native code remains **unverified**; do not represent a local library lock as a solved universal fallback. |
| Atomic operation boundaries in Python built-ins | Compiler-recognized bounded commits with slow callback/revalidation paths | Tier/deopt/signal/trace/GC tests proving no partially committed state or duplicated side effect; retain most algorithms in Python. |
| Native/JIT race-detector coverage | Instrument supported C/native lanes now in the future harness; design JIT/fasm visibility explicitly | A demonstrated deliberate-race positive control and deliberate-synchronization negative control through generated code and M:N switches. Without them, a clean TSan log leaves a material gap. |
| Release validation scale | Start with finite seeds/thread counts in §4, expand based on failures and coverage | Reproducible corpus manifests, zero unexplained failures, explicit exclusions and reviewed upstream-bug ledger. No amount of passing unsupported toy code substitutes for NumPy's whole suite. |

The most consequential unresolved issue is the interaction of unrestricted
C-visible memory access with concurrent Python mutation, moving GC and native
thread affinity. The source shows why per-object synchronization, lifetime
protection, extension declarations and test coverage are all required. It
does not provide a generic adapter that solves arbitrary unsafe extension
code without a GIL. This report leaves that obligation explicit while giving
concrete implementation paths and acceptance tests for supported production
behavior.
