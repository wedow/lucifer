---
id: luc-1rwj
status: open
deps: []
links: []
created: 2026-11-24T13:54:58Z
type: task
priority: 1
tags: [perf, interp, kernel, startup]
---
# Cold and builtin-heavy code: where the gap to CPython goes

Measurement and diagnosis only. Why Lucifer beats CPython on small hot kernels but loses on real programs (pyperformance, CLI corpus, imports): the breakdown by interpreter, cold builtins, hot builtins, startup and one CLI program, and a ranked list of causes. Notes below.


## Notes (2026-11-24)

Method. Master at 87e14dd7, built as the landing build does. Baseline is CPython 3.14.10t
(free-threaded, `--enable-optimizations=no`, `-O2`, no LTO, computed gotos, no tail-call
interpreter); Lucifer is also `-O2 -g`, no PGO/LTO, computed gotos, frame pointers. Each
number is one pinned run: `perf stat` user-space instructions (the steady column) and
cycles, and `perf record` on instructions (Lucifer frame-pointer chains, CPython LBR
chains). Samples are mapped to opcodes by source line (the opcode label around the
first eval-loop frame, callees included). CPython bytecode counts come from opcode
tracing (the bytecode is the same in both runtimes). Startup is subtracted where a
number says so.

### A. Interpreter, JIT off

| workload | bytecodes | instr/bc Luc | instr/bc CPy | cyc/bc Luc | cyc/bc CPy |
|---|---|---|---|---|---|
| fib(27) | 7.3M | 104.8 | 47.1 | 67.7 | 30.6 |
| arith while-loop | 30M | 113.8 | 62.4 | 45.6 | 19.2 |
| instance attributes | 30M | 105.4 | 45.1 | 41.4 | 14.4 |
| dict get/set | 21M | 133 | 77 | | |
| for-range/enumerate | 13.8M | 162 | 89 | | |

Instructions per execution, by opcode (callees included):

| opcode | Luc | CPy | note |
|---|---|---|---|
| CALL + RESUME + RETURN (Py to Py) | 654 | 208 | generic call path: `call_tail`, `lu_frame_call`, `bind`, `alloc_frame`; no exact-args form |
| BINARY_OP (int) | 129-264 | 51-128 | no specialized forms; `lu_long_binary` builds results through digit arrays (`PyLong_FromLongLong` -> `lu_long_from_digits`), `%` runs the general `long_divmod` |
| STORE_ATTR instance | 346 | 83 | `lu_inline_store` converts ints to the unboxed field kind (`value_kind`, `PyLong_AsLongLongAndOverflow`, `_PyNumber_Index`) |
| LOAD_ATTR instance | 84 | 35 | re-boxes the unboxed field |
| COMPARE_OP (int) | 80-116 | 53-56 | |
| LOAD_GLOBAL | 79 | 33 | |
| LOAD_FAST | 40 | 10 | owned reference (biased incref, decref later); CPython 3.14 borrows (`LOAD_FAST_BORROW`: 12 machine instructions against 21) |
| POP_JUMP_IF_* | 47-51 | 19-24 | `lu_jit_observe_branch` writes the cache word on every branch, JIT off too |

Where the extra instructions go (sum over fib, arith, attr: gap 4.08G instructions):

| mechanism | share of gap |
|---|---|
| int operations in C (general bigint routines for compact ints, unboxed field conversion) | 40% |
| opcode bodies and dispatch (generic paths, 12-instruction dispatch with `prev_instr`/`cur_instr` tracking and `oparg`, `localsplus`, target table spilled to the stack) | 38% |
| type-feedback hooks with the JIT off (`lu_jit_observe` out of line, `lu_jit_observe_branch`, `lu_site_read`) | 9% |
| allocator and dealloc | 6% |
| frame push/pop/bind | 4% |
| reference counting | 1.5% (CPython spends as much on stackrefs) |

Specialization coverage: Lucifer has 9 specialized forms in 3 families (LOAD_GLOBAL, LOAD_ATTR,
STORE_ATTR). CPython 3.14 has 84 in 17 families; the hot ones Lucifer lacks are CALL (20),
BINARY_OP (15), TO_BOOL (6), FOR_ITER (4), COMPARE_OP (3), UNPACK_SEQUENCE (3), STORE_SUBSCR (2)
and CONTAINS_OP (2). Polls (`lu_qsbr_quiescent` plus the poll word) cost under 1%.

### B. Cold builtins

Every method of str, list, dict, set, frozenset, tuple, int, bool, float, bytes and bytearray
is kernel Python (`lib/kernel/*.py`, frozen into the code image, installed by `@kernel_type`
into the static types). So is every builtins function: 37 in `builtins.py`, and the other 8
(`__build_class__`, ascii, format, iter, next, repr, sorted, open) in other kernel files.
The only C left in those type dicts is `list.__sizeof__` and the `__class_getitem__`s.
The interpreter calls 10 kernel functions natively (`lu_native_call`: dict.get, list.append,
len, isinstance, getattr, hasattr, setattr, index, bytearray.append, dict.items). The C
accelerators CPython ships are Python too (`lib/modules`, 22k lines: _sre 3.6k, _io 3.2k,
_codecs, posix, itertools, _json, _collections, _struct, _functools, _abc, _operator...).

Instructions per call, net of the loop (micro-loop of 20k and 60k iterations):

| call | CPy | Luc off | off/CPy | Luc JIT on | on/CPy |
|---|---|---|---|---|---|
| str(i) | 905 | 21,921 | 24x | 619 | 0.7x |
| s.upper() | 416 | 3,487 | 8.4x | 420 | 1.0x |
| s.strip() | 308 | 3,520 | 11x | 272 | 0.9x |
| s.find("7") | 444 | 6,858 | 15x | 371 | 0.8x |
| len(s) | 180 | 489 | 2.7x | 10 | 0.06x |
| l.append(i) | 219 | 715 | 3.3x | 424 | 1.9x |
| l10.index(5) | 1,101 | 22,659 | 21x | 1,163 | 1.1x |
| l100[10:20] | 839 | 1,109 | 1.3x | 1,178 | 1.4x |
| sum(l100), per item | 83 | 2,194 | 26x | 62 | 0.7x |
| sort(reverse=True) of 1000 sorted, per item | 60 | 6,794 | 113x | 908 | 15x |
| d.get(k, 0) | 699 | 1,225 | 1.8x | 607 | 0.9x |
| d[k] = v | 603 | 913 | 1.5x | 679 | 1.1x |

A str method is two or three interpreted frames (`upper(self, *args, **kwargs)` builds a tuple and
a dict, `_noargs`, then the primitive) at about 650 instructions per frame; per-item builtins
(sort, sum, index) run their loop in interpreted Python. In the strops and listops profiles
(JIT off) the primitives' own C is a few percent; the eval loop's own code is 45-57%,
reference counting 15-16%, frames 4-11%.

With the JIT on: a code compiles after 1,000 calls or 1,000 back-edges (plus 4 per code unit
beyond 256 units), synchronously on the calling thread; kernel functions compile inlined into
their callers. Compile counts and time (`-X lucifer_jit_stats`): bare start 15 units 2.2 ms;
stdlib imports 172 units 78 ms (3.7 MB code); pyflakes 523 units 907 ms (38 MB code, 5,734
inlined calls). Nothing compiled is kept between processes: the image holds bytecode only, and
the JIT writes no cache.

### C. Hot builtins, JIT on

listops (JIT on 1.40G instructions, CPython 0.56G): compiled code 43%, primitives 12%, JIT
helpers 9%, interpreter 8%, critical sections 7% (`PyCriticalSection_BeginMutex`/`End` around
each list primitive), allocator 4%. The sort dominates: 600k items x 908 = 0.54G, against
CPython's 60 per item; the append loop adds 424 per call against 219 (generic `h_prim_call`
plus a critical section per primitive call). strops (0.79x CPython): `format_decimal` (str(i))
7%, `lu_object_new` 6%, `prim_str_case` 5%, `index_arg` 5%.

### D. Startup and import

Bare `-c pass`: CPython 52M instructions, Lucifer 154M off, 173M on.

| phase (Lucifer, JIT off) | instructions |
|---|---|
| site import | 67M |
| importlib setup (`lu_import_init`: _bootstrap/_external install) | 33M |
| kernel boot (kernel module bodies run, slots inherited) | 24M |
| standard streams (`_io` in Python) | 13M |
| finalize | 6M |
| sys configure | 5M |
| image map, native init | 5M |

In CPython's bare start the interpreter is 4% (2M instructions); the rest is C type and
module init. In Lucifer the eval loop is 38% (58M) and dict lookups 17%: startup runs the kernel,
importlib, `_io` and site as interpreted Python, and nothing is snapshotted (the image holds
code objects only). The JIT adds 19M instructions and 30M cycles: 15 compiles (3% of
instructions), `lu_jit_enter` tries and `lu_jit_observe` (7.5%).

Stdlib imports (json, re, asyncio, typing, dataclasses, collections, functools, pathlib,
subprocess, email.parser): CPython 388M, Lucifer 1.60G off, 1.65G on. Stdlib bytecode
comes from `.pyc` files (cache tag cpython-314, written at build time; with
`PYTHONDONTWRITEBYTECODE=1` neither runtime writes new ones, so site-packages are
compiled from source on every run in both). The bytecode compiler is C (CPython's parser
and compiler, `runtime/compiler`) and costs the same as CPython's (22M vs 26M; it compiles
dataclasses' and namedtuple's generated source). Marshal 21M vs 12M. The eval loop is 720M
against CPython's 39M: Lucifer interprets what CPython does in C (class creation and slot
updates in the kernel, `__import__` in the kernel, `_imp`, `_io`, `posix`, `_abc`, `_sre`,
`_functools` ports). importlib's bootstrap is the same frozen Python in both. JIT on: the
JIT compiler is 17% of the run (288M) and nothing is won back.

### E. pyflakes (the cli corpus invocation, on email, json and asyncio)

| | instructions | cycles |
|---|---|---|
| CPython | 4.36G | 3.0-3.6G |
| Lucifer JIT off | 33.8G | 20.5-28.9G |
| Lucifer JIT on | 16.0G | 14.1-14.6G (4.3x) |

| kind (share of the run) | CPython | Lucifer on | Lucifer off |
|---|---|---|---|
| JIT compiler | | 3.24G (20%) | |
| compiled code | | 2.38G (15%) | |
| JIT helpers, exits, entry | | 1.68G (11%) | |
| interpreter + frames + calls | 0.93G (21%) | 2.30G (14%) | 21.0G (62%) |
| C runtime (objects, primitives, C-API, types) | 2.56G (59%) | 4.5G (28%) | 9.3G (27%) |
| bytecode compiler (C) | 0.61G (14%) | 0.72G (4.5%) | 0.74G (2%) |
| allocator + GC | 0.17G (4%) | 0.57G (3.6%) | 1.8G (5%) |
| startup (boot, site) | 0.05G | 0.17G | 0.17G |

`type_lookup` alone is 6.3% of the JIT-on run (1.0G), mostly called from compiled kernel
code through `prim_type_lookup`; whether these are method-cache misses was not measured.
Of pyflakes' 11.6G-instruction gap with the JIT on: compiling 28%, compiled code plus helpers
doing what CPython's interpreter does in 0.93G 27%, cold interpretation (A and B) 20%, the C
runtime layer 16%, startup 1%.

### Ranked causes of the gap on cold and builtin-heavy code

1. **Builtins run as interpreted kernel bytecode until they compile (B).** 8-26x CPython per
   call and 26-113x per item for loops in Python (sort, sum, index); 62% of pyflakes with the
   JIT off, 20% of its gap with it on, most of the import gap (D). Mechanism: kernel code that
   runs compiled from its first call: units for the kernel compiled at build time and kept in
   the image beside the bytecode (relocated and guarded like JIT units), so no process pays
   either the interpretation or the compile. About 2,500-4,000 lines (estimate).
2. **JIT compile cost on programs that are mostly cold (D, E).** 28% of pyflakes' gap
   (523 units, 907 ms, 73 KB of code per unit); 17% of the import run, with no return. Mechanism:
   compile only what pays back: a threshold and inline budget scaled by measured compile cost
   against the run count, and cause 1, which removes the kernel's share. 150-400 lines.
3. **Compiled code no faster than CPython's interpreter on real, polymorphic code (C).**
   27% of pyflakes' gap; list.sort 15x CPython per item, list.append 1.9x. Mechanism: primitive
   calls from compiled code by direct typed call instead of the generic `h_prim_call`, and the
   critical section's uncontended or owned-object fast path emitted in line. 300-600 lines.
4. **The interpreter's per-bytecode cost, 2.2-2.4x CPython (A).** Int paths 40% of it, generic
   opcode bodies and dispatch 38%, JIT feedback hooks paid with the JIT off 9%, allocation 6%.
   Mechanism: quickened forms for BINARY_OP/COMPARE_OP on compact ints and floats, CALL of a
   Python function with exact arguments, kernel calls, FOR_ITER over range/list/tuple,
   STORE_SUBSCR and TO_BOOL (800-1,200 lines); compact-int results built directly, not
   through digit arrays (about 100 lines); the attribute store's boxed path when the field
   holds a box (about 100 lines); feedback hooks behind the code's warm flag (about 50 lines).
5. **Startup runs the kernel, importlib, `_io` and site as Python (D).** 102M instructions per
   process more than CPython (3x); 1% of pyflakes, all of `-c pass`. Mechanism: a heap snapshot
   of the booted interpreter (deferred in DESIGN §3.4/§10). About 2,000-3,000 lines.
6. **The C runtime layer under kernel code (E).** 1.9G more than CPython's C in pyflakes (16%),
   `type_lookup` the largest single item. Not diagnosed further.

The line counts are estimates. The shares hold for the named workloads only.

### Not measured

- Python-level attribution inside Lucifer (kernel vs user code, import machinery vs module
  bodies): kernel frames are hidden from settrace/monitoring, and I did not use `-X perf`
  because it changes how frames run. JIT units show only as anonymous code.
- Kernel vs user share of the JIT's compile time.
- Exact per-opcode counts for Lucifer (sampled attribution; no single-stepping).
- CPython's startup by phase (LBR chains too short to reach the init functions).
- Cycles are noisy (one run per arm); use the instruction columns.
