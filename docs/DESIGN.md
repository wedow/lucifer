# Lucifer design

Status: design of record for the first milestones. Inputs: `docs/research/01..06`,
`docs/validation.md`, CPython v3.14.10 (`third_party/cpython`), the CPython v3.14.0
headers, the pinned cp314t oracle and NumPy 2.5.4 cp314t wheel, and the donor
Smalltalk VM at `origin/master` 88754eb (read only through `git show`/`ls-tree`).

The bar is fixed (`README.md`): CPython 3.14 semantics; full non-limited C-API on
the free-threaded ABI (cp314t) from day one; no GIL; built-in types and stdlib
mostly Python compiled by our JIT over a small native primitive set; numpy's own
suite passing against the unmodified cp314t wheel. This document decides how.

## 0. Decisions

| # | Decision | Choice | Deciding evidence |
|---|---|---|---|
| 1 | Runtime language | **C11** for all native code (ABI layer, interpreter, GC, threads, primitives, JIT). No fasm, Zig or Rust. | 1,843 exported `Py*`/`_Py*` symbols, 33 C-varargs entry points, 539 layout definitions and 448 inline wrappers fixed by headers; extensions need glibc's loader and TLS. C compiles the canonical headers unchanged; every other option re-derives them. §2 |
| 2 | Execution format | **CPython 3.14 bytecode**, the same marshal format and magic number (3627), and `cache_tag` `cpython-314`. | Code objects, `dis`, frames, tracebacks, `.pyc`, `PyCodeObject` (`cpython/code.h`) and the monitoring API are all defined over it. Byte-identical output from the host's `compile()` is a free oracle. §3 |
| 3 | Compiler | Python: a tokenizer, a pegen-generated parser, then a port of symtable/codegen/flowgraph/assemble. It must produce output byte-identical to CPython's. | 17.3k lines of C (`Python/{compile,codegen,flowgraph,assemble,symtable,ast_preprocess}.c`). The generator is `Tools/peg_generator/pegen/python_generator.py`. §3 |
| 4 | Bootstrap | The host CPython 3.14 compiles the kernel and stdlib to `.pyc`/frozen marshal at build time. Self-hosting means Lucifer reproduces those bytes. There is no heap snapshot at first. | Bytecode is CPython's, so the host is a perfect stage-0 compiler. Snapshots wait on a startup measurement. §3 |
| 5 | Object model | The cp314t headers are canonical. Every materialized object sits at a stable address with the exact layout. Unboxing and scalar replacement happen only inside JIT code, before escape. | `PyObject` is 32 B on t vs 16 B on GIL builds. `PyTypeObject` symbols are 0x1b0 B in the oracle. Research 01 §6–7. §4 |
| 6 | Memory | Vendored mimalloc with per-carrier heaps (cp314t's allocator); biased refcounting exactly as `Include/refcount.h`; trial-deletion cycle collector over the GC heaps; QSBR for container storage. | Extensions hold references the runtime cannot see (statics, module state), so only refcount-informed collection is sound. Root tracing is not. §4.3 |
| 7 | Builtins | Python "kernel" classes installed into the static C type objects. C-API functions are primitives where they only touch layout. Otherwise they are thunks into the kernel. Kernel frames are invisible. | Research 03 §5/§7. One semantic source per operation. §4.4 |
| 8 | Threads | **Tasks on carriers from K1.** A task (`PyThreadState` plus heap frames) is a Python thread of control. A carrier (pthread) owns what the ABI ties to the OS thread: `ob_tid`, mimalloc heaps, BRC, QSBR, TLS. A degenerate scheduler binds one task per carrier for life (cp314t's 1:1) until M1.8 lets unpinned tasks migrate. | `_Py_ThreadId()` inlines `%fs:0` into extension code, so ownership belongs to the carrier. Retrofitting M:N onto native-stack execution failed repeatedly in the donor's predecessors. §5 |
| 9 | Tiers | A C interpreter with inline caches (the reference and the deopt target), then a block-SSA optimizing JIT in C ported from the donor. A template tier only if warm-up data asks for one. | The donor's own roadmap keeps the interpreter as the reference/deopt landing. §6 |
| 10 | Build | `make`. Python runs only in the bootstrap freeze/compile step (host CPython 3.14, later Lucifer itself). | §7 |

## 1. Denominators

Counts from the pinned artifacts (commands in §12).

| Quantity | Count | Source |
|---|---:|---|
| Dynamic symbols exported by `python3.14t` | 1,856 | `nm -D --defined-only` on the oracle (libpython is static in the executable) |
| … of which `Py*` / `_Py*` | 1,843 (1,284 public, 559 private) | same |
| … public `Py*` functions | 1,106 | type `T` |
| … `Py*`/`_Py*` data objects (types, singletons, exceptions) | 215 | types `D`/`B`/`R` |
| Exported functions with a C-varargs prototype | 33 | prototypes ending in `...` in `Include/**` |
| NumPy 2.5.4 cp314t: native binaries / undefined symbols | 19 `.so` / 734 | `nm -D --undefined-only`; 3 bundled libs (OpenBLAS, gfortran, quadmath) |
| … imported `Py*`/`_Py*` | 411 (346 functions, 65 data) | same |
| … private `_Py*` | 28, incl. `_Py_DecRefShared`, `_Py_MergeZeroLocalRefcount`, `_Py_SetImmortal`, `_PyType_Lookup`, `_PyObject_GetDictPtr`, 8 `_PyUnicode_Is*` | same |
| … varargs used | 13: `PyArg_ParseTuple[AndKeywords]`, `PyArg_UnpackTuple`, `Py_BuildValue`, `PyErr_Format`, `PyErr_WarnFormat`, `PyOS_snprintf`, `PyObject_Call{Function,FunctionObjArgs,Method,MethodObjArgs}`, `PyTuple_Pack`, `PyUnicode_FromFormat` | same |
| Remaining 323 NumPy imports | libc/libm/libgfortran/OpenBLAS | resolved by glibc `ld.so`, not by us |
| Smallest oracle stdlib extension | `_statistics`: 8 `Py*` imports; `_bisect` 28; `_heapq` 19; `_testcapi` 547 | oracle `lib-dynload` |
| CPython 3.14 opcodes | 241 defined (base < 129, specialized 129–233, instrumented ≥ 234) | `Include/opcode_ids.h` |
| difftest probe+bridge import closure | 64 modules; 42 Python files, 22,238 lines | `-X importtime` on the oracle |
| `import numpy` closure | 172 modules (needs `_ctypes`, `math`, `_struct`, `_pickle`/`pickle`, `_datetime`/`_pydatetime`, `_ast`, `_tokenize`, `_opcode`, `_typing`) | same |
| `import pytest, numpy.testing` closure | 376 modules | same |
| Harness denominators | difftest 164; libtest 56,200; numpytest 52,273 (oracle t: 51,140 PASS); AWFY 14; pyperformance 9; oracle check 11 | `results/*/expected.tsv` |
| Donor VM | 210,057 lines fasm in 215 `vm/*.inc|asm`; SSA optimizer 56,465 / 51 files; scheduler 13,575; GC 7,349 | `git ls-tree` at 88754eb; research 04 §1 |

## 2. Implementation language

The native runtime has four jobs: (a) export the cp314t ABI, (b) host dlopen'd
extensions that bind against the executable, (c) run the interpreter, GC and
threads, and (d) generate machine code.

| Requirement | C | fasm | Zig | Rust |
|---|---|---|---|---|
| 1,843 symbols with exact prototypes | `#include "Python.h"`; the compiler checks every definition against the canonical header | Unchecked by hand; prototypes not machine-checked | `@cImport` works, but `translate-c` drops many macros | `bindgen` cannot translate `static inline` (`Py_INCREF` and most of `refcount.h` are inline in 3.14) |
| 539 layout definitions, 448 inline wrappers, 215 static objects | Same headers, so `PyObject_HEAD_INIT` and `offsetof` are the ABI by construction | Needs a generated offsets include, which means a C step anyway; 215 static initializers by hand | Same as C via cImport, with translation gaps | Same gaps; shims in C anyway |
| 33 varargs callees (`PyArg_ParseTuple`, `Py_BuildValue`, `PyErr_Format`, …) | Native `va_list`; CPython's `getargs.c`/`modsupport.c` (3,536 lines) adapt directly | Hand-written SysV register-save area and `va_arg` walker per callee | `@cVaStart` exists; x86-64 support has been unreliable (unverified for current Zig) | Defining C-variadic functions is unstable (`c_variadic`, nightly only) |
| dlopen + glibc TLS + `-rdynamic` export | Standard | Donor is a freestanding static ELF with zero libc (`docs/CONVENTIONS.md`, INV-ELF). That policy is incompatible: extensions call libc and read `%fs:0`. | Possible | Possible |
| JIT code generator | Port the donor's algorithms; `offsetof` gives every field the JIT touches | Donor code exists, but research 04 §1 rates every tier "adapt/redesign": templates name `FH_*`, `P_*`, `gc_*` contracts, so lifting it means re-deriving all of them | Possible | Possible |
| Toolchain on host | gcc and clang present | fasm present | not installed | rustc present |

**Decision: C only.** The ABI is defined in C, so C is the only language in which
conformance is checked by the compiler rather than by review. A C/fasm mix would
need every struct the JIT touches mirrored into fasm includes, and nothing the
donor contains can be linked unchanged (research 04 §6). The donor's value
transfers as algorithms, invariants and test method (§6.3), ported to C. The
little assembly we need (deopt trampolines, under 200 lines; there is no stack switching, §5.2)
lives in `.S` files assembled by the same `cc`.

Experiment that could reintroduce fasm: if the template tier is ever justified
(§10), its copy-and-patch templates could be fasm `format binary` blobs with a
generated offsets include. That decision waits for the template-tier data.

## 3. Execution format, compiler, bootstrap

### 3.1 Bytecode

Lucifer executes CPython 3.14 bytecode: the same opcodes, `co_code` with inline
`CACHE` entries, exception tables, line and position tables, the same `PyCodeObject`
layout (`Include/cpython/code.h` `_PyCode_DEF`) and the same `.pyc` header and
marshal format. What comes for free:

- `dis`, `inspect`, `traceback`, `linecache`, `code`, `codeop`, `py_compile`,
  `compileall` and `importlib`'s `SourceLoader` run unmodified.
- Cython/NumPy calls such as `PyCode_NewEmpty`, `PyUnstable_Code_New*`,
  `PyFrame_New` and `PyTraceBack_Here` get real objects.
- `.pyc` files are interchangeable with CPython's. That gives a cross-check
  (§3.4) and lets `make` precompile the stdlib with the host.

Our own IR exists only inside the JIT (§6). A private bytecode would make all of
the above emulation work and buys the JIT nothing: the donor also compiles from
bytecode plus type feedback. The decision is not close.

The interpreter may rewrite instructions into its own specialized forms (the
129–233 range is invisible except through `dis(adaptive=True)`). It need not
replicate CPython's specialization set. Lucifer-private opcodes never appear in
user-visible code objects.

### 3.2 Compiler in Python (`lib/compiler/`)

| Stage | Source of truth | Lucifer file | Method |
|---|---|---|---|
| Tokenizer | `Parser/lexer/*.c` (1,859 lines) | `tokenizer.py` | Port. It also provides `_tokenize` for `tokenize.py`, which uses the C tokenizer since 3.12. |
| Parser | `Grammar/python.gram` (1,569 lines, C actions) + `Parser/action_helpers.c` | `python.gram` (Python actions), generated `parser.py`, `helpers.py` | Translate the actions; generate with pegen's `python_generator.py` at dev time; commit the output (CPython commits `parser.c` the same way); `make regen` checks it is current. |
| AST | `Parser/Python.asdl` (154 lines) | generated `_ast.py` | Node classes with `_fields`, `_attributes`, `__match_args__`. |
| Symtable, AST optimizer, codegen, CFG optimizer, assembler | `Python/{symtable,ast_preprocess,codegen,compile,flowgraph,assemble}.c` (17,350 lines) | `symtable.py`, `preprocess.py`, `codegen.py`, `flowgraph.py`, `assemble.py` | Faithful port, so output is byte-identical. |
| Entry | `builtins.compile`, `exec`, `eval`, import | `__init__.py` | Called by the kernel's `compile`. |

The compiler is ordinary Python that depends only on the kernel and `_ast`. It
is developed and tested on the host CPython before Lucifer can run it, against a
byte-for-byte oracle:

```
make check-compiler   # host python3.14 runs lib/compiler on each input;
                      # marshal.dumps(ours) == marshal.dumps(compile(src)), and
                      # ast.dump(ours, include_attributes=True) == ast.dump(ast.parse(src))
```

### 3.3 Bootstrap and build-time host

The bootstrap needs a CPython 3.14.x (`HOST_PYTHON`, default `python3`, checked by
magic number). The host only compiles; no host object reaches Lucifer except as
marshal bytes.

1. `tools/freeze` compiles the **frozen set** with the host's `compile()` and
   emits `build/frozen.c` (marshal blobs). The frozen set is `lib/kernel/**`,
   `lib/compiler/**`, the `lib/modules/` ports, and the CPython startup modules
   CPython itself freezes (`importlib._bootstrap[_external]`, `zipimport`, `codecs`,
   `encodings{,.aliases,.utf_8}`, `io`, `abc`, `site`, `os`, `posixpath`, `stat`,
   `genericpath`, `_collections_abc`, `_sitebuiltins`).
2. `tools/freeze --pyc` copies `third_party/cpython/Lib` plus the `lib/modules`
   overlay into `build/lib/python3.14t/` and writes `__pycache__/*.cpython-314.pyc`
   with the host. This is what `make install` does for CPython; the oracle ships
   these files too. It matters because difftest and the benchmark harnesses set
   `PYTHONDONTWRITEBYTECODE=1`, and recompiling `re`/`enum`/`json` through an
   interpreted compiler on every start would blow the 5-second probe budget.
3. `cc` builds `runtime/**` + `third_party/mimalloc` + `build/frozen.c` into
   `bin/lucifer`, linked `-rdynamic -ldl -lpthread -lm`.

The frozen bundle is loaded by a native marshal reader (`runtime/interp/marshal.c`,
adapted from `Python/marshal.c` `r_object`). The kernel cannot unmarshal itself
before it exists. The same reader backs `marshal.loads`; `marshal.dumps` is
Python (`lib/modules/marshal.py`, from PyPy `lib_pypy/_marshal.py`, 691 lines).

### 3.4 Self-hosting

| Stage | Compiler of the frozen set / `.pyc` | Check |
|---|---|---|
| 0 | host CPython 3.14 | `check-compiler` on the host: ours ≡ CPython's |
| 1 | `bin/lucifer` compiles at runtime (`-c`, `exec`, uncached imports) | difftest; libtest `test_compile`, `test_dis`, `test_ast`, `test_grammar` |
| 2 | `make selfhost`: `HOST_PYTHON=bin/lucifer tools/freeze` | bundle and every `.pyc` byte-identical to stage 0, so stage 0 is reproducible without CPython |

After stage 2 the host is a convenience. Any 3.14 compiler, including a previous
`bin/lucifer`, can bootstrap. A heap snapshot image (research 03 §8) is deferred.
The decision comes from data: see §10 for the startup experiment.

## 4. Object model and memory

### 4.1 Layout

- **Headers are the definition.** `include/` is a verbatim copy of CPython
  v3.14.10 `Include/` (public and `cpython/`; `internal/` excluded) plus the cp314t
  `pyconfig.h` (`Py_GIL_DISABLED 1`, no paths). The runtime compiles against those
  same headers. An extension's inlined `Py_INCREF`, `PyTuple_GET_ITEM` or
  `PyUnicode_DATA` therefore reads what the runtime wrote.
- **Exact payloads** for every type in research 01 Appendix A: int (`lv_tag` +
  30-bit digits), float, complex, bytes (inline `ob_sval`), bytearray, str (compact
  ASCII/UCS1/2/4 with the free-threaded `state.interned` byte), tuple (`ob_hash` +
  inline items), list (`ob_item` array), dict/set headers, cell, method, function,
  code, slice, exceptions, memoryview/`Py_buffer`, datetime C-API structs, type and
  heap-type objects, `PyMethodDef`/`PyModuleDef`/`PyMemberDef`.
- **ABI structs that are not objects** keep their prefixes. `PyThreadState` is
  `struct _ts` from `cpython/pystate.h`; Cython's fast path reads
  `current_exception` directly. Also `PyMutex` (1 byte, `_PyMutex_Lock` slow path),
  `PyCriticalSection[2]`, `Py_buffer`, `PyType_Spec`.
- **Private layouts are ours** wherever 3.14 made them opaque: dict keys tables,
  interpreter frames (`PyFrameObject` is opaque in 3.14), generator/coroutine
  bodies, `PyInterpreterState`, and heap-type instance attribute storage. Heap
  types use `Py_TPFLAGS_MANAGED_DICT | Py_TPFLAGS_MANAGED_WEAKREF` with the
  two-word preheader, so `_PyObject_GetDictPtr` and `tp_dictoffset == -1` behave
  as in CPython. Attribute values sit inline behind shared keys. Extensions reach
  them only through functions, so the JIT may use shape-guarded fixed offsets there
  (the donor's flat-layout idea where the ABI allows it).
- **Static data.** All 215 exported data objects (`PyFloat_Type`,
  `_Py_NoneStruct`, `PyExc_*`, small-int cache −5..256, …) are statically
  initialized immortal C objects in `runtime/types/static.c`. They have the oracle's
  exact symbol sizes (`nm -S`: `PyFloat_Type` 0x1b0, `_Py_NoneStruct` 0x20,
  `_Py_TrueStruct` 0x30). The kernel fills in their slots at startup; their dicts live in per-interpreter state, so `tp_dict` stays NULL as in the oracle (`docs/FOUNDATIONS.md` §2.1).
- **Hashes and table orders are bit-exact.** str/bytes use SipHash-1-3 (key zero
  under `PYTHONHASHSEED=0`, which every harness sets); int hashes mod 2^61−1;
  float hashes as `_Py_HashDouble`; tuples use xxHash. The set table replicates
  `Objects/setobject.c` probing (linear probes, resize thresholds), so set iteration
  order matches the oracle's printed output.

### 4.2 Allocation and reference counting

| Mechanism | Design | Source |
|---|---|---|
| Allocator | mimalloc as vendored by CPython (9,770 lines, MIT), with the cp314t heap split per carrier (§5.3): objects, GC objects, GC objects with preheader, raw memory. `PyObject_Malloc`/`PyMem_Malloc` go to mimalloc; `PyMem_RawMalloc` goes to libc. The GC enumerates objects by walking GC heaps, so there is no per-object GC list. | `Objects/mimalloc`, `Objects/obmalloc.c` |
| Refcount | Exact `Include/refcount.h` cp314t semantics. `ob_tid` is the allocating carrier's TCB (`%fs:0`); local non-atomic increments by that carrier, shared atomic otherwise, including from a migrated task. `_Py_DecRefShared` and `_Py_MergeZeroLocalRefcount` are exported. Biased-refcount merge queues are per carrier. | `Python/brc.c` (208 lines) port |
| Immortals | `ob_ref_local == UINT32_MAX` for statics, interned strings and small ints; `_Py_SetImmortal` exported. | `refcount.h` |
| Deferred refcount | Functions, code, modules, heap types and top-level globals use cp314t deferred counting. Stack references from interpreter and JIT frames are not counted, so the GC scans every task's frames, running or parked. This is where precise stack maps (donor INV-EXE-1, INV-GC-1) become mandatory. | `_PyObject_SetDeferredRefcount` |
| Storage reclamation | Replaced `ob_item` arrays and dict keys tables are freed after a QSBR grace period. Lock-free readers (`list[i]`, dict lookup) never see freed storage. | `Python/qsbr.c` (282) port; donor `vm_retire`/`vm_grace_step` |
| Uniqueness | `_PyObject_IsUniquelyReferenced` follows cp314t (owner + local 1 + shared 0). `PyUnstable_Object_IsUniqueReferencedTemporary` consults interpreter and JIT frames. NumPy's in-place temporary elision depends on it. Another carrier's objects answer false (§5.3). | research 01 §6.2 |

### 4.3 Cycle collector

Root tracing is unsound here: an extension's statics and module state hold
references the runtime cannot enumerate. The 22 NumPy native binaries do this
throughout. Lucifer therefore uses cp314t's trial deletion
(`Python/gc_free_threading.c`, 2,954 lines, ported):

1. Stop the world (§5). Walk the mimalloc GC heaps.
2. For each tracked object, compute `gc_refs = refcount − internal references`
   via `tp_traverse`. Add deferred stack roots found in task frames (§5.6).
3. Anything with `gc_refs > 0`, plus everything reachable from it, survives. Run
   `tp_finalize` on the rest (PEP 442), handle resurrection, clear weakrefs, then
   `tp_clear`.

Traverse functions for builtin layouts are generic C. A static reference map per
layout says which words are `PyObject*` (the donor's field-kind map), so
Python-written builtins need no per-type traverse code. Finalizer timing equals
cp314t's: prompt on refcount zero, deferred for cycles and cross-thread merges.
The JIT may delete balanced inc/dec pairs and never allocate virtual objects. It
may not change when a materialized object dies (research 01 §6.2).

### 4.4 Python-written builtins and the primitive boundary

```
user code ─▶ bytecode ─▶ interpreter / JIT ─┬─▶ type slots (tp_*, nb_*, sq_*, mp_*)
                                              │      ├─ C extension: native function
C extension ─▶ C-API function ─▶ thunk ───────┤      └─ builtin: slot thunk ─▶ kernel Python
                                  └─ primitive ┴─▶ _lucifer primitives (C, layout-aware)
```

- **Kernel.** `lib/kernel/*.py` defines `object`, `type`, `int`, `bool`, `float`,
  `complex`, `str`, `bytes`, `bytearray`, `tuple`, `list`, `dict`, `set`,
  `frozenset`, `range`, `slice`, `memoryview`, function/method/descriptor types,
  the exception hierarchy, iterators and `builtins` functions. A decorator such as
  `@kernel_type(list)` installs each class body into the corresponding static type
  object: methods as `PyMethodDescrObject`, dunders as slots via the
  `Objects/typeobject.c` slotdefs table, ported to Python. Type slots stay the single
  dispatch authority for interpreter, JIT and C callers.
- **Kernel functions look native.** They are `PyCFunctionObject` /
  `PyMethodDescrObject` whose `ml_meth` is a generic vectorcall trampoline; the code
  object hangs off a private tail. Their frames are elided from `f_back`,
  tracebacks, `sys._getframe` and tracing. Profilers see `c_call`/`c_return`. So
  `type(list.append)` is `method_descriptor` and a `KeyError` from `dict.__getitem__`
  shows only the user frame, as the difftest traceback programs require.
- **Primitives** are the `_lucifer` builtin module in C (`runtime/prim/`). Each one
  has a descriptor: argument/result kinds, may-raise, may-allocate, may-reenter,
  may-block (parks or brackets, §5.5), thread-scoped (§5.4), safepoint, effects. Kernel code calls them as ordinary functions; the JIT inlines
  them as intrinsics. Families follow research 03 §5: object/layout (allocate a
  layout, read/write a field, incref/decref, type and identity); scalars (compact-int
  fast arithmetic with overflow flag, digit-vector kernels, IEEE ops, `dtoa`
  shortest repr and correctly rounded parse, from `Python/dtoa.c`); storage (ref
  arrays, 1/2/4-byte char storage, copy/fill/compare/find with refcount-correct
  bulk moves); namespace dict (lookup/insert/delete for exact-str keys, used by
  `LOAD_GLOBAL`, `LOAD_ATTR` on modules/types and `_PyType_Lookup`); sync (`PyMutex`,
  critical sections, atomics, park/unpark); control (call and generator resume, which return a
  frame to push, §5.2; raise, frame materialization, weakref, attach/detach); platform (fd/fs/process/time/signal/
  socket, `dlopen`/`dlsym`, `fork_exec`). Target: at most 300 kernel primitives
  plus platform adapters, counted by `tools/census` (§11).
- **No bootstrap regress.** Interpreter namespace operations call the namespace
  primitives directly, never Python dict code. Python dict code handles general
  keys (`__hash__`/`__eq__` callbacks with mutation re-checks), resize policy,
  views and methods, over the same keys-table format defined once in
  `runtime/object/dict.h`.
- **Rule for C-API functions.** A C-API function that only builds or reads a layout
  (`PyLong_FromLong`, `PyFloat_AsDouble`, `PyTuple_New`, `PyList_GET_SIZE`
  semantics, `PyUnicode_FromStringAndSize`) is a primitive. Anything with Python
  semantics (`PyObject_GetAttr`, `PyNumber_Add`, `PyObject_RichCompare`,
  `PyDict_SetItem` with a non-str key, `PyObject_Repr`) is a thunk into the kernel
  implementation. One implementation per semantic; the JIT later gives thunks
  compiled entry points. `getargs.c`, `modsupport.c` and `PyUnicode_FromFormat` are
  adapted CPython C because they consume `va_list`.
- **Atomicity.** Every primitive leaves its object traversable and structurally
  valid. Safepoints occur only between primitive calls, at calls and at back-edges.
  Kernel mutators that CPython makes atomic (`list.append`, `dict.__setitem__`,
  `set.add`, …) bracket their commit with the same `PyCriticalSection` C uses, so
  extension code and kernel code exclude each other. Hash and `__eq__` callbacks
  run outside commit regions and revalidate afterwards (research 02 §3.3).

### 4.5 JIT representation freedom

Inside compiled code: compact ints as machine words with an overflow guard;
floats in XMM registers; tuples, iterators, bound methods, frames and short-lived
objects as virtual (scalar-replaced) values. Before any C call, heap store,
unknown call, deopt or observation point, the value is materialized at a stable
address with the exact layout. Materialization happens once, so later identity
checks see the same object. Unboxed fields in heap objects are out of scope until
identity tests justify them (`a.x is a.x` stays true).

## 5. Execution model: tasks on carriers

Every Python thread of control is a **task**. Every OS thread that runs one is a
**carrier**. From K1 the scheduler is degenerate: one task per carrier, bound for
life, which is exactly cp314t's 1:1 threads. Every structural precondition for
M:N exists from the first interpreter, so turning on migration (rung M1.8) is a
policy change with tests, not a rewrite.

### 5.1 Why it is designed in

| Source | What failed or forced a redesign | Rule taken |
|---|---|---|
| Donor's predecessor VMs (owner report; not in the donor's history) | M:N retrofitted onto execution state that lived on native stacks failed several times. The donor was restarted with the shapes designed in: Processes never correspond to native stacks, worker and execution state stay separable, stacks are VM-managed chunks (donor `docs/00-CONSTITUTION.md` §XVI, items 128–135). | Execution state is off the C stack from K1 (§5.2). |
| Donor, current docs (shapes that work) | No register bank or native SP is continuation state at a park (INV-SCH-8, INV-EXE-1). A completion carries a never-reused wait identity (INV-SCH-14, `docs/vm/wait-DESIGN.md`). A stop aimed at one Process diverts only that Process (INV-SCH-11). A stop is complete only when every owed acknowledgement arrived (INV-SCH-12). Per-worker `runnext` and local queues with stealing of runnable work only (`docs/vm/execution-model-DESIGN.md` §5.1). | §5.5 wait records, §5.6 per-carrier acks and targeted stops, affinity-first queues. |
| Donor, places where state still leaked native | Compiled activations rode the worker stack, so deep recursion segfaulted at 8k–65k levels while heap-framed interpretation had no bound (7987473ae). A global FIFO plus a service-thread wake relay migrated 75% of dispatches at 4 workers and caused 10–20% cross-worker young roots. Delivery on the running worker and per-worker queues cut ping-pong migrations from 20,097 to 1 (`execution-model-DESIGN.md` §0, 901baae09, d1fcd6b9a). | No Python recursion in C. Affinity is the default, because a migrated task loses biased refcounting (§5.3). |
| Java Loom ([JEP 444](https://openjdk.org/jeps/444), [JEP 491](https://openjdk.org/jeps/491)) | Virtual threads pin inside native frames and inside `synchronized`, because monitors were owned by the carrier. A blocking pinned thread captures its carrier. Fixing monitors took a JDK release (24). | Every lock owner is the task from day one. Native frames pin. Pinned blocking blocks only its own carrier. |
| Go ([`runtime/proc.go`](https://go.dev/src/runtime/proc.go) `entersyscall`/`handoffp`, [`cgocall.go`](https://go.dev/src/runtime/cgocall.go), [Go 1.4 notes](https://go.dev/doc/go1.4#runtime)) | Segmented stacks were replaced by copied runtime stacks (hot split). A blocking syscall or cgo call stays on its M while sysmon hands the P to another M after about 20 µs. A C→Go callback locks the goroutine to its thread. `LockOSThread` serves thread-affine C libraries. | A blocking call runs in place and the run queue moves (§5.5). C callbacks pin. Thread-scoped primitives pin for life (§5.4). |
| Stackless and greenlet ([greenlet docs](https://greenlet.readthedocs.io/en/latest/)) | Hard switching copies C stack slices. That breaks C code holding stack addresses, and greenlets cannot cross threads. Only soft switching (a non-recursive eval) is cheap and safe. | The C stack is never switched or copied. An unpinned park returns to the carrier loop. |
| CPython 3.11+ (`Include/internal/pycore_interpframe_structs.h`, `Python/ceval.c`, `Python/bytecodes.c`) | `_PyInterpreterFrame`s live on per-thread `_PyStackChunk`s. `_PUSH_FRAME` calls Python without recursing. Generator frames are embedded in the generator and resumed by `SEND_GEN`/`FOR_ITER_GEN` via `DISPATCH_INLINED`. A `FRAME_OWNED_BY_INTERPRETER` entry frame marks each C entry. Slot wrappers (`slot_nb_add`), `__del__` and C-API calls still recurse, and 3.14 bounds them with `c_stack_soft_limit`. | Copy the frame model. Close the recursion CPython keeps by putting protocols in kernel Python. |

The Loom, Go and greenlet rows come from public documentation and were not
re-verified for this document (unverified).

### 5.2 Execution state off the C stack

- **Frames** are private structs on the task's chunked frame stack
  (`datastack_*`). Generator and coroutine frames are embedded in their objects.
  `PyFrameObject` is created lazily.
- **One eval-loop activation per task.** These push or pop frames in that
  activation, with no C recursion:
  - calls, returns and exception unwinding across Python frames;
  - generator and coroutine resumption by `FOR_ITER`, `SEND`, `await`,
    `next()`, `send`, `throw` and `close`;
  - class bodies, `exec`/`eval`, and imports of Python modules;
  - every kernel builtin.

  The mechanism has two parts:
  - Call and resume primitives return a frame for the loop to push. They never
    call the interpreter.
  - Slot protocols with Python implementations are kernel Python (binary-op
    reflection, rich comparison, `__getattribute__`→`__getattr__`, `__init__`
    after `__new__`), so for the interpreter they are ordinary calls.
- **C recursion remains only where C calls Python.** These run a nested eval
  loop on the carrier's C stack and pin the task for their duration (§5.4):
  - extension code calling the C-API (`PyObject_Call*`, `PyIter_Next` on a
    generator, slot calls made from C);
  - extension module init;
  - `tp_finalize`/`__del__` and weakref callbacks run at a refcount-zero decref,
    which keeps cp314t's prompt finalization;
  - C-level trace and profile functions, `sys.monitoring` callbacks and signal
    handlers.
- **Depth accounting.** `py_recursion_remaining` counts Python frames and is
  task state. C recursion is bounded by the carrier's `c_stack_soft_limit`. It
  raises `RecursionError` at nested-eval entry, as 3.14 does.
- **JIT frames** obey INV-EXE-1. Before any park or safepoint acknowledgement,
  every frame is published. Native registers and SP are never continuation
  state.
- **No stack switching.** An unpinned park makes the eval loop return to the
  carrier's loop. Resuming means entering the loop at the task's top frame.
  There is no context-switch assembly.

### 5.3 Task and carrier

The task record is a `PyThreadState` (`struct _ts`) plus a private task tail.
The carrier record is per pthread, reached through one TLS word.
`PyThreadState_Get()` and `PyGILState_GetThisThreadState()` return the task bound
to the calling carrier. A task with a C frame cannot be rebound, so an extension
sees one stable tstate for its whole native extent.

| Owner | State |
|---|---|
| Task (travels with it) | `interp`, `prev`/`next` (the interpreter's task list), `_status.{initialized,finalizing,cleared,finalized}`, `_whence`, `id`, `thread_id` (§5.7), `py_recursion_remaining`/`_limit`, `recursion_headroom`, `tracing`, `what_event`, `c_tracefunc`/`c_profilefunc` and their objects, `current_frame`, `datastack_chunk`/`_top`/`_limit`, `current_exception`, `exc_info`, `exc_state`, `async_exc`, `dict`, `delete_later`, `context`, `context_ver`, `coroutine_origin_tracking_depth`, `async_gen_firstiter`/`_finalizer`, `current_executor`, `dict_global_version`, `threading_local_key`/`_sentinel`, `remote_debugger_support`, `critical_section` (always empty at a migration point). Tail: `asyncio_running_loop`/`_task`, `Py_tss_t` values, pending targeted events, pin state. |
| Carrier: tstate fields rewritten at bind, meaningful only while bound | `state` (attached/detached), `native_thread_id`, `gilstate_counter`, `_status.{bound,unbound,bound_gilstate,active}`, `holds_gil` (always 0), `eval_breaker` (mirrors the carrier's poll word) |
| Carrier record | biased-refcount identity (`ob_tid` is this pthread's TCB, the `%fs:0` that `_Py_ThreadId()` inlines into extensions); mimalloc heaps and BRC merge queue, both per (carrier, interpreter) (`docs/FOUNDATIONS.md` §2.1); freelists; per-thread heap-type refcount array; QSBR record; GC allocation counters; native stack bounds and C-stack references; poll word; run queue; glibc TLS, `errno` |

**Objects do not belong to tasks.**
- `ob_tid` names the carrier that allocated the object, and migration changes no
  object.
- On a new carrier, the task's references to older objects take the shared
  (atomic) refcount path, which is correct and slower.
- Zero-local merges queue to the owning carrier, which drains its BRC queue at
  every poll and before it sleeps. A retiring carrier merges everything it owns
  and abandons its heaps, which is cp314t's thread-exit path.
- New allocations go to the current carrier's heaps. Remote frees use
  mimalloc's cross-thread path.
- `_PyObject_IsUniquelyReferenced` answers false for another carrier's objects.
  A migrated task therefore gets the conservative answer, and NumPy elides fewer
  temporaries.

### 5.4 Pinning

A task may leave its carrier only at a park or a safepoint, and only when
`pin == 0` and it is not pinned for life.

- **Transient pin.** `pin` is one word in the task tail.
  - It is incremented when the eval loop calls foreign C (an extension function,
    method, slot or vectorcall, or a `ctypes` call) and when C enters a nested
    eval loop. It is decremented on return.
  - Kernel primitives that neither call Python nor block are not counted.
  - The cost is one increment and one decrement per foreign call, at the same
    place 3.14 checks the C stack.
  - `Py_BEGIN_ALLOW_THREADS` regions are inside C, so they are pinned by
    construction.
- **Lifetime pin.** These tasks never migrate:
  - tasks attached for a foreign OS thread (`PyGILState_Ensure`,
    `PyThreadState_New`);
  - the main task (signal masks, Tk and other main-thread C libraries);
  - any task that calls a platform primitive whose descriptor is
    `thread_scoped`: `os.sched_setaffinity(0, …)`, `signal.pthread_sigmask`,
    `os.unshare`/`setns`, and the like.
- **A pinned park blocks its carrier in place** on that carrier's futex and
  counts as detached. That is exactly cp314t.
- **Invariants at a migration point:**
  - no C frame;
  - no critical section held (kernel commit regions contain no safepoint, and a
    section suspended by a detach is inside C);
  - no open QSBR read interval (primitives close theirs before returning).
- **Identity-keyed state uses the task ident, never the carrier.** This covers
  the owners of `RLock`, `_thread.lock` and import locks, `threading.local`, and
  `Py_tss_t`. It is Loom's monitor lesson.

### 5.5 Park and unpark

Every blocking operation goes through one of two entry points from K1:

- **`lu_park(wait, deadline) → result`** is for waits that someone else
  completes.
  - A wait record holds its owner task, a never-reused wait id, a result slot
    and a deadline.
  - `lu_unpark(id, result)` may come from any thread. Stale and duplicate ids
    are ignored, and an early result is retained.
  - An unpinned task parks with its frames published, and the eval loop returns
    to the carrier. Unpark queues the task on the carrier that last ran it.
  - A pinned task's carrier sleeps on its futex instead.
  - The ported parking lot (`Python/parking_lot.c`) sits on these calls. So do
    `PyMutex`, critical-section contention, `_thread.lock`, `RLock`,
    `Condition`, `queue`, `Event` and `join`. `time.sleep` and every timeout
    are deadlines.
- **`lu_syscall_begin()`/`lu_syscall_end()`** bracket every blocking syscall.
  - This is cp314t's detach/attach, the same thing `Py_BEGIN_ALLOW_THREADS`
    does.
  - The call runs in place, so `errno`, TLS and signal semantics are untouched.
  - With migration on, a monitor hands a carrier's runnable tasks to a spare
    carrier once the carrier has spent about 20 µs inside a bracket (Go
    `handoffp`).
  - Users: file I/O, `waitpid`, `getaddrinfo`, `select`/`poll`/`epoll_wait`,
    socket calls, `flock` and tty reads.
- **io_uring comes later and is measured.** Socket, poll, pidfd and file
  operations move from the bracket to `lu_park` over a per-carrier ring. The
  running carrier submits and reaps, as in donor `execution-model-DESIGN.md`
  §5.1. Callers see the same API.
- **K1 minimum:**
  - `lu_park`/`lu_unpark` over futexes, with deadlines;
  - the parking lot and `PyMutex` on top of them;
  - the bracket around every blocking syscall in `runtime/prim/os.c`;
  - a `make check` lint that rejects any blocking libc or syscall call outside
    the bracketed wrappers.

### 5.6 Safepoints and stop-the-world

- **Polls.** Each carrier has one poll word. The interpreter checks it at calls
  and back-edges (the eval breaker). JIT code checks it with a two-instruction
  poll (the donor's `sc_next` pattern).
- **Stop-the-world**, used by GC, `settrace`/`setprofile` across all threads and
  code invalidation:
  - A request bumps a global stop sequence.
  - It completes when every attached carrier has acknowledged after publishing
    its task's frames.
  - Detached carriers (in a syscall bracket, in a pinned park, or idle) count as
    stopped. They wait at re-attach until the world restarts.
  - Tasks never acknowledge. A running task is covered by its carrier's ack.
    Parked and runnable tasks are quiescent by construction, so the cost is
    O(carriers), not O(tasks).
- **Stale and aimed requests.**
  - A stop-the-world sets the please-stop reason only on a carrier whose task is
    attached. A task that detached first is stopped by the requester's retry
    (every millisecond), and its stale reason finds no request at the next poll,
    which then merely detaches and attaches.
  - A task created during a stop starts suspended, and one deleted during it is
    no longer waited for. Both are decided under the task list lock.
  - A park returns `LU_PARK_INTR` only when a signal handler left the signals
    reason in the carrier's word, which only the main thread's carrier has. Other
    interruptions resume the wait.
- **Walks over all tasks.** GC roots, `threading.settrace_all_threads` and
  `setprofile_all_threads`, and frame inspection walk the interpreter's task
  list, in every state.
- **Targeted events** set the task's pending bit and, if the task is bound, its
  carrier's poll word. There is no global bump. This covers
  `PyThreadState_SetAsyncExc`, per-task tracing and debugger stops. Binding
  re-raises the poll word if bits are pending.
- **Signals.** Handlers run in the main task at its safepoints
  (`Modules/signalmodule.c` semantics, policy in Python).
- **Fairness (M1.8).** A monitor sets the preempt bit on a carrier whose task
  has run 10 ms while other tasks are runnable (Go sysmon). It is a no-op in the
  degenerate scheduler.
- **Locks.** `PyMutex` and critical sections are ported from
  `Python/{lock,parking_lot,critical_section}.c` (1,228 lines). The byte lives in
  `ob_mutex`, which extensions also take.
- **Extensions without `Py_mod_gil`** load with the GIL still disabled, which is
  CPython 3.14t's `PYTHON_GIL=0` behavior (§11 item 3). NumPy declares
  `Py_MOD_GIL_NOT_USED`.

### 5.7 `threading.Thread` and lightweight tasks

| Surface | Degenerate (K1 to M1.8) | Migration on (M1.8) |
|---|---|---|
| `threading.Thread`, `_thread.start_joinable_thread` | a task plus a new carrier pthread, bound for life | a task on the carrier pool, pinned only while `pin > 0` or for life. Pool size comes from the affinity mask and the cgroup quota, and handoff (§5.5) adds carriers. |
| `_thread.get_ident()` | the carrier's `pthread_self()`, as in cp314t | the task ident: nonzero and unique among live tasks; may be reused after exit, as in CPython |
| `threading.get_native_id()` | the carrier's TID | the current carrier's TID. `Thread.native_id` keeps the value from start, so for an unpinned thread the two can differ. That is the one observable divergence, and a lifetime pin makes them agree from then on. |
| `threading.local`, `Py_tss_t`, contextvars, exception and trace state | per task | per task |
| C `thread_local` in extensions | the thread's own | the carrier's: right for caches (BLAS buffers, `errno`), wrong for identity |
| Foreign threads, the main thread | pinned for life | pinned for life |
| `threading.stack_size()` | sizes the carrier | sizes new carriers; bounds only C recursion |

Research 02 §3.2 marks transparent migration of arbitrary extension TLS as
unverified. M1.8 decides it from data. If a corpus lane diverges because an
extension keys identity on C TLS, calling into that extension becomes a
lifetime pin, recorded as a module-level flag. It never becomes a global
fallback.

**No lightweight-task API.** Once migration is on, a `threading.Thread` costs a
task: a tstate and heap frames. Tens of thousands of threads then come through
the standard API, and a private API would be a non-CPython surface that no
program uses. asyncio stays the Python event loop. A native loop over `lu_park`
and io_uring is a §10 experiment after M1.8.

### 5.8 Proving the preconditions while the scheduler is degenerate

- **Deep recursion.** `check-kernel` runs every program under a 512 KiB
  `RLIMIT_STACK`. `tests/kernel/deep_recursion.py`:
  - recurses 150,000 Python frames deep under `sys.setrecursionlimit(200_000)`;
  - drives a 10,000-deep `yield from` chain;
  - unwinds an exception through 100,000 frames.

  It is written first and shown passing on the oracle under the same limit.
- **Handoff stress, `-X lucifer_handoff`.** This is a scheduler policy, not a
  test hook. It starts a second carrier and lifts the main-task pin. Every
  safepoint with `pin == 0` then parks the task and unparks it on the other
  carrier, through the same `lu_park`, run-queue and bind code that M1.8
  migration uses.
  - Every `tests/kernel` program under the flag matches the oracle.
  - `tests/kernel/handoff_witness.py` prints how many distinct
    `_thread.get_native_id()` values it saw over 1,000 loop iterations. It
    expects 2 under the flag and 1 without.

## 6. Tiers

### 6.1 Interpreter (`runtime/interp/`)

- One C file, `ceval.c`, with computed-goto dispatch over all 3.14 base opcodes.
  Semantics come from `Python/bytecodes.c` (5,551 lines), written by hand. The
  cases generator is not used: its output embeds CPython internals and would put
  Python into the runtime build.
- Frames are private structs on per-task chunked stacks. `PyFrameObject` is
  created lazily on observation.
- The loop is non-recursive for Python-to-Python calls, generators and
  coroutines. It returns to the carrier on an unpinned park and recurses only
  under C (§5.2).
- Inline caches live in the `CACHE` words, as in CPython. Specialization rewrites
  opcodes with atomic stores, valid under concurrent execution. Guards are
  `tp_version_tag`, dict keys versions, `func_version` and global-dict watcher
  tags. These caches are also the JIT's type feedback: a site's identity is
  (code, offset), the donor's caller+opcode site.
- Fast paths for exact builtin types call primitives directly
  (`BINARY_OP_ADD_INT` → compact add, `LOAD_GLOBAL` → namespace lookup). Everything
  else goes through type slots.
- The interpreter is the reference semantics and the only deopt landing.
  `sys.settrace` and `sys.monitoring` instrument it and invalidate JIT code.

### 6.2 Optimizing tier (`runtime/jit/`)

Block SSA in C, ported from the donor's backend (`docs/vm/optimizer-backend-DESIGN-a.md`):

1. **Trigger and build.** Back-edge and call counters trigger compilation. A
   background compiler thread builds SSA from bytecode plus inline-cache feedback.
   It inlines kernel functions, so `list.append` and `dict.get` written in Python
   compile into the caller. That is the payoff of Python builtins.
2. **Guards.** Type version, keys version, function version, global watcher tag,
   value range. Effects are explicit per Python operation: descriptors,
   `__eq__`/`__hash__` callbacks, C calls and allocation can all reenter.
3. **Representation.** Unboxing, escape analysis and scalar replacement (§4.5);
   ownership-based refcount elision (Cinder precedent, research 01 §5).
4. **Backend.** Per-block linear-scan register allocation over GPR/XMM with spill
   homes and edge reconciliation (port of `optimizer_ssa_allocate.inc`); x86-64
   encoder in C; root maps per safepoint; perf map and jitdump (port of
   `perf_map.inc`).
5. **Entry.** OSR at `JUMP_BACKWARD`.
6. **Deopt.** At an unexecuted bytecode boundary (the donor's BEFORE-state rule,
   `optimizer_ssa_emit.inc` `opt_ssa_deopt`). Reconstruct interpreter frames,
   including inlined kernel frames, then materialize virtual objects once with
   aliases preserved. Completed side effects are never replayed.
7. **Publication and invalidation.** Code is published by an epoch-checked
   atomic store into the code object's private tail. Invalidation sets a flag that
   compiled code checks at guards and polls. Dependency lists (donor
   `optimizer_dependencies.inc`) mark code dead when a type, keys or global
   version changes. Patching running code waits for a measured need.

### 6.3 What comes from the donor

No donor file is linked. Each row is a port or a reuse of method.

| Donor piece | Lucifer use | Form |
|---|---|---|
| Interpreter-as-reference, deopt landing, INV-EXE-1 continuation rule | §6.1, §6.2 step 6, §5 safepoints | invariant |
| Block SSA IR, effects, guards, inlining, `optimizer_ssa_escape.inc` scalar replacement | `runtime/jit/ssa*.c` | port to C, Python lowering new |
| `optimizer_ssa_allocate.inc` (1,579 lines) | register allocator | port |
| `optimizer_ssa_float.inc` raw binary64 handling | float unboxing | port; donor INV-REP-1 float identity semantics rejected |
| `optimizer_background.inc`, epoch-checked publication, dependency invalidation | background compile, code publication | port |
| `source_send_cache.inc` site identity, advisory profiling, exit-driven recompile | inline-cache feedback | adapt |
| Field-kind maps (`field_kinds.inc`) | GC reference maps per layout; heap-type inline values | adapt |
| `vm_retire`/`vm_grace_step` | QSBR (cp314t `qsbr.c` is the code) | method |
| Epoch stop protocol (`sc_poll_core`, exact-once ACK), targeted stops (INV-SCH-11) | per-carrier stop-the-world, task events (§5.6) | method |
| `perf_map.inc` (531 lines) | perf map/jitdump writer | port |
| Published continuations (INV-EXE-1, INV-SCH-8), wait identities (`wait-DESIGN.md`), per-worker `runnext`/local queues/stealing (`execution-model-DESIGN.md` §5.1) | task/carrier split, `lu_park`, M1.8 scheduler (§5) | method |
| Per-worker io_uring, snapshots | io_uring after M1.8 by measurement (§10); no snapshots | later |
| Template tier `tc.inc` | none at first | §10 experiment |
| Copying nurseries, 8-byte header, tagged immediates in the heap, freestanding ELF | rejected | conflict with stable `PyObject*` and glibc hosting |
| Test and measurement method (scenario harness, exact-output oracles, AWFY sampler, gates) | already in `tools/` | copied |

## 7. Repository layout and build

```
Makefile                 all, check, check-compiler, check-kernel, check-sched, check-capi, abi-audit, selfhost, regen, check-harness
include/                 verbatim CPython v3.14.10 Include/ (no internal/) + cp314t pyconfig.h
runtime/
  main.c                 argv → config (Python/initconfig.c semantics) → kernel bootstrap → lucifer startup
  object/                alloc.c (mimalloc heaps) refcount.c brc.c gc.c qsbr.c dict.h layouts.c
  types/                 static.c (215 exported data objects) slots.c (slot thunks)
  capi/                  one file per header family: abstract.c long.c float.c unicode.c bytes.c tuple.c list.c
                         dict.c set.c object.c type.c call.c errors.c import.c module.c getargs.c modsupport.c
                         buffer.c weakref.c pystate.c capsule.c datetime.c … (every exported symbol)
  interp/                ceval.c frame.c specialize.c code.c marshal.c frozen.c(loader) exceptions.c
  prim/                  _lucifer: object.c int.c float.c dtoa.c str.c storage.c dict.c sync.c os.c loader.c
  thread/                task.c carrier.c sched.c park.c lock.c parking_lot.c critical_section.c stw.c
  jit/                   (from rung P0) ssa*.c regalloc.c x86.c deopt.c perfmap.c
  asm/                   deopt.S
lib/
  kernel/                Python builtins (§4.4) + sys/builtins module population + startup
  modules/               Python replacements for CPython C modules (§9.3), e.g. _io.py marshal.py _sre.py itertools.py
  compiler/              tokenizer, python.gram, parser.py (generated), symtable, codegen, flowgraph, assemble, _ast.py
  _sysconfigdata_t_linux_x86_64-linux-gnu.py   ABIFLAGS 't', Py_GIL_DISABLED 1, EXT_SUFFIX .cpython-314t-x86_64-linux-gnu.so
third_party/cpython/     existing vendored Lib/ (unchanged; CHANGES.md records adapted C sources)
third_party/mimalloc/    from CPython v3.14.10 Objects/mimalloc (MIT)
tools/freeze             bootstrap compile: frozen.c and build/lib .pyc
tools/compilercheck      host lane for check-compiler
tools/abi                abi-audit: exported set and symbol sizes vs results/abi/cp314t-exports.tsv
tools/census             Python vs native line and primitive counts
tests/kernel/            binary-level programs run with -I -S against the oracle
tests/capi/              programs importing unmodified oracle lib-dynload extensions
tests/sched/             M1.8 programs: many tasks on few carriers, pinned callbacks
build/                   ignored: frozen.c, objects, lib/python3.14t with __pycache__
bin/lucifer              ignored build output
```

`make` builds with `cc` only, after the bootstrap step. Adapted CPython C
(`marshal.c` reader, `dtoa.c`, `getargs.c`, `modsupport.c`, `lock.c`,
`parking_lot.c`, `critical_section.c`, `brc.c`, `qsbr.c`, `gc_free_threading.c`)
keeps its PSF notice and is listed in `third_party/cpython/CHANGES.md`.

`bin/lucifer` reports `sys.version_info == (3, 14, 10, 'final', 0)`,
`sys.abiflags == 't'`, `sys._is_gil_enabled() == False`, `Py_GIL_DISABLED == 1`,
`sys.implementation.cache_tag == 'cpython-314'`, and
`_imp.extension_suffixes() == ['.cpython-314t-x86_64-linux-gnu.so', '.abi3.so', '.so']`
(the oracle's list). `sys.prefix` is resolved from the
executable to `build/lib/python3.14t`. It must accept the CPython command line:
`-c`, `-m`, `-I`, `-S`, `-E`, `-B`, `-u`, `-W`, `-X`, script, `.pyc` and stdin.

| Harness | What `--python bin/lucifer` needs |
|---|---|
| `tools/difftest` | Probe `-c 'import json, sys, sysconfig; …'`; `bridge.py` → `runpy.run_path`; exact stdout/stderr/exit; startup under 5 s. |
| `tools/oracle/check --disable-gil` | Version, ABI and GIL rows; `ssl`, `sqlite3`, `ctypes`, `zlib`, `lzma`, `bz2`, `readline`, `decimal`(`_decimal`) imports (owner decision 4). |
| `tools/libtest` | Runs regrtest itself with a `-j 1` worker subprocess. Needs `subprocess`/`_posixsubprocess` (native `fork_exec` primitive), `unittest`, `faulthandler`, `tempfile`, `json`. |
| `tools/numpytest` | `PYTHONPATH=$SITE_T`; dlopen of the wheel's 19 `.so`; pytest stack (376-module closure); `--parallel` uses 4 threads in one process. |
| `tools/bench/{awfy,pyperformance}` | AWFY: plain Python. pyperformance: pyperf with worker subprocesses and `statistics`. |
| `tools/ftcorpus`, `tools/pypi` | Threads under stress; venv creation (`pyvenv.cfg` in startup). |

## 8. Milestone ladder

Every rung is a score from a committed harness. "Oracle" means
cpython-3.14.10t. Rungs on different rows of the dependency column can run in
parallel.

| Rung | Harness and command | Acceptance score | Depends on |
|---|---|---|---|
| C0 | `make check-compiler` (host) | 100% byte-identical marshal and AST for the 42 closure modules, 164 difftest programs and `tests/kernel`; report the share of all vendored `Lib/**/*.py` the host compiles | none |
| K0 | `make abi-audit` | `bin/lucifer` exports all 1,843 `Py*`/`_Py*` symbols; all 215 data symbols have the oracle's `nm -S` sizes; unimplemented functions abort naming themselves and are counted | none |
| K1 | `make check-kernel` | every `tests/kernel` program (≥ 30) run as host-compiled `.pyc` under `-I -S` with a 512 KiB stack limit matches the oracle on stdout, stderr and exit, both plain and under `-X lucifer_handoff`, including `deep_recursion` and `handoff_witness` (§5.8) | K0 |
| M0.0 | `tools/difftest run --python bin/lucifer` | probe accepted: 0 NOTRUN of 164 | C0, K1 |
| M0.1 | same | 46 named programs PASS (§9.4) | M0.0 |
| M0.2 | same | 164/164 PASS (adds cycle GC, weakrefs/finalizers, generators/async, float formatting, traceback printing, metaclasses, `memoryview`, pattern matching, generated and AWFY lanes) | M0.1 |
| S1 | `make selfhost` | stage-2 bundle and all `.pyc` byte-identical to stage 0 | M0.1 |
| M1.1 | `tools/libtest run --python bin/lucifer --modules <17 core>` | ≥ 1,902 PASS (90%) of the oracle's 2,113 across test_int, test_long, test_float, test_bool, test_list, test_tuple, test_dict, test_set, test_str, test_bytes, test_types, test_class, test_descr, test_exceptions, test_generators, test_scope, test_grammar | M0.2 |
| M1.2 | `make check-capi` | unmodified oracle `_statistics`, `_bisect` and `_heapq` import, and their `tests/capi` programs match the oracle | M0.1 |
| M1.3 | libtest `test_capi` with the oracle-built `_testcapi`/`_testlimitedcapi` | ≥ 538 PASS (50% of the oracle's 1,075) | M1.1, M1.2 |
| M1.4 | `tools/numpytest run --collect --python bin/lucifer` | `import numpy` and pytest collection succeed under Lucifer, so rows leave NOTRUN; ≥ 1 PASS | M1.2, M1.1 |
| M1.5 | `tools/numpytest run` | ≥ 13,069 PASS (25%) | M1.4 |
| M1.6 | same | ≥ 39,205 PASS (75%), 0 CRASH | M1.5 |
| M1.7 | serial and `--parallel` | every case the current cpython-3.14.10t oracle manifest marks PASS is PASS (oracle parity; 51,140 as of the 11-02 widened baseline), no FAIL/ERROR/TIMEOUT/CRASH, GIL disabled throughout. **This is the public bar.** | M1.6, M1.3 |
| M1.8 | `make check-sched`; ftcorpus serial/parallel; `tools/numpytest run --parallel`, all with migration on | `tests/sched/many_tasks.py` (10,000 threads ping-ponging over `Condition`s and socketpairs, plus sleeps) matches the oracle with `-X lucifer_carriers=4` while the runner samples at most 16 OS threads; `tests/sched/pinned_callback.py` (a `ctypes` `qsort` comparator that blocks on a lock released by another thread) sees one `get_native_id()` across each callback while 1,000 other threads migrate; ftcorpus and numpytest PASS sets equal M1.7's; `tests/kernel` and difftest pass under `-X lucifer_handoff` | M1.7; every blocking primitive on `lu_park` or the bracket (§5.5) |
| P0 | `tools/bench/awfy --python bin/lucifer` | 14/14 PASS with the SSA tier on (no speed target) | M0.2 |
| P1 | `tools/bench/compare --suite awfy` | geomean time/CPython ≤ 1.189 (parity with the oracle) | P0 |
| P2 | same | ≤ 0.594 (2× faster than the oracle); pyperformance 9/9 PASS | P1 |
| P3 | same | ≤ 0.119 (10× faster than the oracle). The donor runs AWFY about 23× faster than CPython. | P2 |

Foundation rungs (K0.1, K1.1, M0.3, M1.1a, M1.6a, L1–L3) and the shapes they carry are in `docs/FOUNDATIONS.md` §4.

Later rungs (no tickets yet): oracle check 11/11; libtest full-suite percentages;
ftcorpus serial/parallel parity; the 24 pypi suites; pyperformance against PyPy.

## 9. First vertical slice: C0, K0, K1, M0.0, M0.1

Goal: `tools/difftest run --oracle "$ORACLE_T" --python bin/lucifer` reports the
probe accepted and 46 PASS. One task (a second carrier only under
`-X lucifer_handoff`), interpreter only, refcount only
(`gc.collect()` returns 0 and collects nothing until M0.2), no JIT. All 1,843
symbols are exported, most as aborting stubs.

### 9.1 C0: compiler on the host

Files: `lib/compiler/{tokenizer,python.gram,parser,helpers,_ast,symtable,preprocess,codegen,flowgraph,assemble,__init__}.py`,
`tools/compilercheck`, a Makefile target.

Mechanism (§3.2): pegen generates `parser.py` from our `python.gram`. The port
order is the dependency order (tokenizer → parser → symtable → codegen →
flowgraph → assemble). Each stage is checked against the host: tokens against
`tokenize`; AST via `ast.dump(include_attributes=True)`; symbols against
`symtable`; code objects via `marshal.dumps` equality, which compares bytecode,
consts, names, line, position and exception tables.

Done when: C0 in §8.

### 9.2 K0: runtime skeleton and the ABI surface

Files: `Makefile`, `include/`, `runtime/types/static.c`, `runtime/capi/*.c`,
`runtime/object/{alloc,refcount,brc}.c`, `third_party/mimalloc`, `tools/abi`,
`results/abi/cp314t-exports.tsv` (name, kind, size from the oracle; no paths).

Mechanism:
- Every exported symbol gets a definition with the header's prototype.
- Unported functions are one-line `LU_UNIMPLEMENTED("PyFoo_Bar")` bodies that
  abort with the name. `tools/abi` counts them.
- Static objects use the canonical `PyVarObject_HEAD_INIT` and are immortal.

Done when: K0 in §8.

### 9.3 K1, then M0.0: interpreter, kernel, startup closure

Files: `runtime/interp/*`, `runtime/prim/*`, `runtime/thread/{task,carrier,sched,park,lock,parking_lot,critical_section}.c`,
`lib/kernel/*`, `lib/modules/*`, `tools/freeze`, `tests/kernel/*.py`.

| Piece | Content |
|---|---|
| Interpreter | Non-recursive for Python calls, generators and coroutines (§5.2). All base opcodes that appear in the C0 corpus; generic slot dispatch; exact-type primitive fast paths; exceptions with tables, tracebacks and chaining; generators, coroutines and `yield from` (`os`, `re` and `enum` use them); closures and cells; class creation with `__build_class__`, `__prepare__`, `__init_subclass__`, `__set_name__` (`enum` needs all of them). |
| Kernel types | Everything listed in §4.4. Exact `int`/`str`/`float` repr and hash; `str` methods used by `re`, `enum`, `json`, `os`, `posixpath`, `sysconfig`. |
| Native modules as Python ports | `marshal` (`dumps`), `_sre` (from PyPy `rpython/rlib/rsre/rsre_core.py` + `rsre_char.py`, 1,934 lines, de-RPython'd), `itertools`, `_collections` (deque, defaultdict; PyPy `lib_pypy/_collections.py` 440 lines), `_functools` (absent: `functools.py` has Python fallbacks), `_abc` (absent: `abc.py` falls back to `_py_abc`), `_io` (from `_pyio`), `_codecs` (utf-8/latin-1/ascii/escape codecs), `_contextvars` (PyPy, 202 lines), `_operator` (absent: `operator.py`), `_stat` (absent: `stat.py`), `_json` (absent: Python scanner), `_types`, `_weakref`, `_thread` (locks, ident, `_local`; no thread start yet), `_signal`, `time`, `errno`, `posix`, `pwd`, `_imp`, `_warnings` (absent: `warnings.py`), `atexit`, `gc`, `_string`, `_sysconfig`, `sys` and `builtins` population. |
| Tasks and carriers | Task record and carrier record split as §5.3; the `pin` counter; the degenerate scheduler; `lu_park`/`lu_unpark` with deadlines under the parking lot; the syscall bracket and its lint; `-X lucifer_handoff` (§5.5, §5.8). |
| Startup | `main.c` parses argv. The kernel loads from frozen, importlib bootstraps, stdio comes from the `_io` port, then `site`, then `-c`/script/`.pyc`, then `sys.excepthook` → `traceback._print_exception_bltin` (as 3.14 does), then `atexit` and exit code. |
| `tests/kernel` | ≥ 30 programs written first, each shown failing: arithmetic and bigints, str/bytes methods, list/dict/set/tuple, classes and inheritance, descriptors, exceptions, generators, closures, comprehensions, `with`, f-strings, `print` with `sep`/`end`/`file`, `deep_recursion`, `handoff_witness`. They run as `.pyc` under `-I -S`, so K1 does not need C0. |

Order inside the slice: K0 → interpreter + kernel to K1 (in parallel with C0) →
startup closure to M0.0 → corpus fixes to M0.1.

### 9.4 M0.1: programs that turn green

Every one imports only `support`, so they need nothing beyond the bridge's
closure:

- int (12): add_overflow, bitwise_negative, bool_subclass, bytes_boundaries,
  float_comparison, floor_mod_signs, index_protocol, multiply_overflow,
  power_modular, reflected_operator, shift_edges, type_change_after_warmup
- dict (9): alias_graph, colliding_keys, equal_distinct_key, hash_exception,
  iterator_resize, iterator_value_change, missing_subclass, reinsert_order,
  views_dynamic
- closure (6): cell_mutation, class_scope, delete_cell, late_binding,
  nonlocal_mutation, recursive_capture
- comprehension (6): class_scope, exception_restore, iterable_once, nested,
  no_leak, walrus_scope
- augassign (6): attribute_order, dict_union, list_alias, notimplemented,
  subscript_order, tuple_item_partial
- sort (5): key_exception, key_mutation, key_once, reverse_stable, stability
- list (2): iteration_mutation, slice_alias

All live under `tests/diff/corpus/` as `<family>_<name>.py`. The other 118 stay
FAIL or PASS opportunistically; none may be NOTRUN after M0.0.

### 9.5 Risks the slice measures first

| Risk | Measurement | Response if it bites |
|---|---|---|
| Interpreted Python builtins make the probe exceed the 5 s startup timeout | `bin/lucifer -X importtime -c "$PROBE"`, plus wall time, on each landing | Coarser primitives on the hot kernel paths first; then the startup image (§10) |
| 32-repetition difftest programs exceed 90 s | per-program seconds in the manifest | the same, then the JIT schedule |
| `_sre` port is slow on `re.compile` at import | importtime rows for `re._compiler` | cache compiled patterns; JIT later |

## 10. Close calls and the experiments that decide them

| Question | Default | Experiment |
|---|---|---|
| Template tier between interpreter and SSA | none | AWFY sampler's warm-up excess cost and first/median ratio, interpreter+SSA vs oracle. Build a tier only if warm-up cost dominates at least 4 of 14 rows after P1. fasm is a candidate for its templates (§2). |
| Startup image vs frozen marshal + `.pyc` | frozen + `.pyc` | `-c pass` and probe wall time broken into kernel init vs imports. Build the image when kernel init exceeds 50% of a `-c pass` that is over 4× the oracle's. |
| C-API thunks vs native duplicates (`PyList_Append`, `PyDict_GetItem`) | thunks | numpytest per-shard wall time against the oracle plus perf profiles. Add a native path only where a thunk dominates a shard, and keep it the same code the JIT inlines. |
| Optimizer written in C vs Python | C | Not close for now. A Python optimizer would compile itself and couple compile latency to warm-up. Revisit only if C SSA maintenance measurably lags. |
| Specialization in shared `co_code` vs per-thread side tables | shared, atomic rewrite (cp314t does this) | `ft-cpython` parallel lane and a specialization-thrash microbench |
| Heap-type field unboxing | off | difftest identity/state programs + AWFY NBody/CD with a flag |
| io_uring waits vs the syscall bracket (§5.5) | bracket | after M1.8: socket and file rows of `many_tasks` and an HTTP lane; move an operation to the ring where it beats the bracket |
| Native asyncio loop over `lu_park` | Python loop | after M1.8: asyncio pyperformance rows; adopt if it beats the selector loop by a measured margin |

## 11. Decisions settled (2026-11-03)

1. **C** is the runtime language (§2). Zig was considered and not adopted: the canonical headers are C, so it would add a toolchain without removing work.
2. **`sys.implementation.name` is `"cpython"`**, and `platform.python_implementation()` returns `"CPython"`, so packaging tags the interpreter cp314t and libraries take their CPython paths. Lucifer identifies itself through `sys.implementation.lucifer` (a version tuple) and `sys.version`'s build string. The goal is a working runtime, and the ecosystem keys on the name.
3. **Extensions without `Py_mod_gil`** load with the GIL off, as CPython 3.14t does under `PYTHON_GIL=0`.
4. **`decimal`** uses `_pydecimal`. The oracle-check row accepts it for Lucifer.
5. **"Mostly Python"** is measured by `tools/census` as proposed. Thresholds get set once the first measured numbers exist.
6. **Adapted CPython C** is allowed where it is native by necessity (§7 list). Each adapted file is recorded in `third_party/cpython/CHANGES.md`.
7. **M:N is structural from K1** (§5). Tasks, carriers, pins, park/unpark and per-carrier stop-the-world are in the first interpreter, under a degenerate one-task-per-carrier scheduler. Migration turns on at M1.8. There is no lightweight-task API: cheap `threading.Thread` is the API (§5.7).

## 12. Reproducing the counts

`ORACLE_T` is the oracle executable `…/bin/python3.14t`, `SITE_T` the numpytest
cp314t venv's `site-packages`, `CPYTHON` a v3.14 source checkout, `PROBE` the
difftest probe text from `tools/difftest/run.py`.

```sh
nm -D --defined-only "$ORACLE_T" | wc -l                                   # 1856
nm -D --defined-only "$ORACLE_T" | awk '$3 ~ /^_?Py/' | wc -l              # 1843
nm -D --defined-only "$ORACLE_T" | awk '$2=="T" && $3 ~ /^Py/' | wc -l     # 1106
nm -D --defined-only "$ORACLE_T" | awk '$2!="T" && $3 ~ /^_?Py/' | wc -l   # 215
find "$SITE_T/numpy" -name '*.so' | wc -l                                  # 19
find "$SITE_T/numpy" -name '*.so' -exec nm -D --undefined-only {} + |
  awk 'NF==2{print $2}' | sort -u | grep -cE '^_?Py'                       # 411 (28 with ^_Py)
nm -D -S --defined-only "$ORACLE_T" | grep -E ' (PyFloat_Type|_Py_NoneStruct)$'   # 0x1b0, 0x20
python3 -I -c 'import re,pathlib,sys; s="".join(p.read_text(errors="ignore") for p in pathlib.Path(sys.argv[1]).rglob("*.h")); s=re.sub(r"/\*.*?\*/","",s,flags=re.S); print(len({m[1] for m in re.finditer(r"PyAPI_FUNC\([^;{]*?\)\s*(\w+)\s*\(([^;{]*?)\)\s*[;A-Z_]",s,re.S) if "..." in m[2]}))' "$CPYTHON/Include"   # 33
"$ORACLE_T" -X importtime -c "$PROBE" 2>&1 >/dev/null | tail -n +2 | wc -l   # 57 (probe); bridge adds the rest of the 64
```
