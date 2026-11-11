# JIT ceiling, 2026-11-11

Status: measurement and verdict. Runtime as of master bda3952. PyPy 3.11
v7.3.23 (the official linux64 release), CPython 3.14.10 (GIL) and 3.14.10t.

## 0. Question and answer

The owner asks whether Python is hostile to a JIT, whether the C-API and
free-threading model is the handicap, or whether our JIT is not doing its
job. Answer:

- **Python is not the obstacle.** PyPy is a JIT with full Python semantics.
  It runs the 13 pyperformance programs of the band at **0.46x** CPython
  3.14t's steady-state time, which is 2.2x faster. Lucifer runs them at
  **4.68x**, about 10x behind PyPy. On AWFY, PyPy is at 0.073 of
  CPython's time and Lucifer at 0.368. PyPy loses to CPython in three
  places:
  - asyncio, which is Python on PyPy and C on CPython (async_tree and
    async_tree_eager_tg are 1.2–1.5x slower);
  - regex-heavy sphinx (1.45x), while docutils is about even (0.89x);
  - one-shot CLI runs that end before it warms up (1.8–3.7x slower).
- **The C-API object model and free-threading do not explain why Lucifer is
  slower than CPython.** CPython 3.14t has the same refcounts, layouts,
  allocator and locks, and it is 4.7x faster than Lucifer. Their cost is
  bounded:
  - The 3.14t build executes 3–16% more instructions than the GIL build in
    the three loops studied here.
  - It takes 12% more time over the 18 programs.
  - In Lucifer's best compiled loop (AWFY Bounce), inline refcounts and
    lock bytes are about 35% of the instructions.
  - These costs put Lucifer's ceiling below PyPy's. They do not put it below
    CPython's.
- **Our JIT is not doing its job on this code.** Per iteration of three hot
  loops, Lucifer executes:
  - 3.3x CPython's instructions on mako, 4.7x on pickle and 2.5x on genshi;
  - 11x, 86x and 16x PyPy's.

  Operations the tier lowers in line beat CPython 4–20x. Every operation
  that reaches a kernel builtin or a Python-written stdlib accelerator loses
  1.5–34x. The excess over CPython has these causes:

  | category | share of the excess |
  |---|---|
  | (d) JIT limitations | ~60% |
  | (c) stdlib in Python | ~35% |
  | (e) other (cycle-collection policy) | ~4% |
  | (a) object model and (b) free-threading | ~1% |

  The (d) limitations are calls the inliner refuses, a generic call
  protocol that binds `*args`/`**kwargs` in C, unspecialized class-level
  lookups, and eval'd code that never compiles. The (c) part is `struct`,
  `BytesIO` and codecs written as generic interpreters. Lucifer spends
  3–15x CPython's instructions on allocation, refcounts and locks, but only
  because the (c) and (d) code allocates, counts and locks far more often
  than CPython's C does.
- **The architecture is wrong for this code in two places, and both are
  fixable without giving up compatibility.**
  - The unit of compilation is the function. Calls between units go
    through a generic protocol, and the inliner gives up at an inlined
    frame's second call. "A call it cannot inline" is 62–97% of the inline
    refusals in the three loops.
  - The kernel and the Python-written accelerators are not written for the
    compiler.

  PyPy shows what the fix looks like. Each of its traces spans the whole
  hot loop: up to 11 functions across user, library, generator and eval'd
  code. Its builtins are interpreter-level code that the JIT sees through.
  Section 4 gives two changes that each give multi-x on the band, a
  third that multiplies the first, and a fourth for the code the tier
  already compiles.
- **On what to expect:** with full C-API compatibility, the realistic
  steady-state target for this band is a little below PyPy's level, about
  1.5–2x faster than CPython. 5–10x on the band is not realistic; PyPy
  itself does not reach it. On AWFY-shaped code, where PyPy is 13.7x
  faster than CPython, 5–10x is plausible.

## 1. The ceiling

The 18 programs of the corpus-gap study ran on all four runtimes in one
session on the bench host, through `tools/bench/corpus`'s own functions:
- each program's reference loop count from `results/status-reference.json`;
- **daily shape**: `-w 1 -n 3`, best value;
- **steady**: `-w 20 -n 5`, best value;
- **CLI programs**: corpus `measure_cli`, the best of 3 runs after a
  warm-up run.

All PyPy CLI outputs match the CPython reference hashes. The pyperformance
programs report one value (seconds per loop), shown here in ms. The CLI
programs report one wall time, also in ms.

| program | PyPy | CPython 3.14 | CPython 3.14t | Lucifer | PyPy ÷ 3.14t | Lucifer ÷ 3.14t | Lucifer ÷ PyPy | daily shape: PyPy ÷ 3.14t |
|---|---|---|---|---|---|---|---|---|
| mako | 6.39 | 13.5 | 16.5 | 55.5 | 0.39 | 3.36 | 8.7 | 0.54 |
| pickle_pure_python | 0.0223 | 0.349 | 0.396 | 2.80 | 0.06 | 7.05 | 125 | 0.11 |
| genshi | 58.9 | 77.8 | 101 | 470 | 0.58 | 4.66 | 8.0 | 0.63 |
| async_tree | 327 | 282 | 279 | 1136 | 1.17 | 4.08 | 3.5 | 1.30 |
| async_tree_io | 403 | 733 | 633 | 2544 | 0.64 | 4.02 | 6.3 | 0.76 |
| async_tree_eager_tg | 326 | 239 | 220 | 1106 | 1.48 | 5.02 | 3.4 | 1.38 |
| chameleon | 3.31 | 15.2 | 18.6 | 130 | 0.18 | 6.99 | 39 | 0.20 |
| typing_runtime_protocols | 0.156 | 0.187 | 0.222 | 1.44 | 0.71 | 6.49 | 9.2 | 3.43 |
| html5lib | 18.9 | 62.2 | 69.2 | 271 | 0.27 | 3.92 | 14 | 0.55 |
| sqlglot_v2 | 41.8 | 119 | 138 | 275 | 0.30 | 1.99 | 6.6 | 0.83 |
| sphinx | 1722 | 1038 | 1190 | 6808 | 1.45 | 5.72 | 4.0 | 2.28 |
| docutils | 2647 | 2566 | 2967 | 18427* | 0.89 | 6.21 | 7.0 | 1.17 |
| coverage | 160 | 547 | 731 | 3278 | 0.22 | 4.48 | 20 | 0.20 |
| cli.black | 1498 | 342 | 400 | 2174 | 3.74 | 5.43 | 1.5 | |
| cli.pyflakes | 1486 | 611 | 627 | 2565 | 2.37 | 4.09 | 1.7 | |
| cli.pycodestyle | 1677 | 673 | 758 | 3268 | 2.21 | 4.31 | 1.9 | |
| cli.markdown | 178 | 86.4 | 102 | 869 | 1.75 | 8.54 | 4.9 | |
| cli.pygments | 780 | 228 | 246 | 2152 | 3.18 | 8.76 | 2.8 | |

\* Lucifer's docutils steady run exceeded the harness's 300 s limit, so
its daily-shape value is shown.

| geomean | PyPy ÷ 3.14t | Lucifer ÷ 3.14t | Lucifer ÷ PyPy |
|---|---|---|---|
| 13 pyperformance programs, steady | **0.46** | **4.68** | **10.2** |
| all 18, steady (CLI whole runs) | 0.74 | 4.99 | 6.7 |
| all 18, daily shape | 1.00 | 4.98 | 5.0 |
| CPython 3.14 (GIL) ÷ 3.14t, all 18 | 0.89 | | |

What the table says:
- **A JIT with full Python semantics beats CPython on the band**:
  - pickle_pure_python 17x;
  - chameleon 5.6x;
  - coverage 4.6x;
  - html5lib 3.7x;
  - sqlglot_v2 3.3x;
  - mako 2.6x;
  - genshi 1.7x.
- **PyPy loses where CPython runs C and PyPy runs Python.**
  - The async_tree family is 1.2–1.5x slower, except async_tree_io (0.64).
    PyPy has no interpreter-level `_asyncio`: Task, Future and the event
    loop run as Python, as they do on Lucifer.
  - The regex-heavy programs gain little or lose: docutils 0.89x, sphinx
    1.45x.
  - So even a mature tracing JIT does not turn Python-written asyncio into
    C-speed asyncio.
- **Warm-up is the other loss.**
  - The daily's one warm-up value is not enough for PyPy on the large
    programs: typing_runtime_protocols 3.4x and sphinx 2.3x in the daily
    shape.
  - The CLI runs are 1.8–3.7x slower than CPython.
  - PyPy has no persistent code cache. Lucifer pays the same price on top
    of its steady-state gap (corpus-gap §3).
- **AWFY** (§1.1): PyPy 0.073, Lucifer 0.368.

### 1.1 AWFY

`tools/bench/awfy` ran PyPy and Lucifer on the bench host; CPython is the
11-11 daily's column. Each cell is the best iteration in ms, at the
canonical inner counts.

| benchmark | CPython 3.14t | PyPy | Lucifer | PyPy ÷ 3.14t | Lucifer ÷ 3.14t | Lucifer ÷ PyPy |
|---|---|---|---|---|---|---|
| Bounce | 1737.6 | 37.4 | 572.7 | 0.022 | 0.330 | 15.3 |
| CD | 2588.9 | 212.1 | 917.0 | 0.082 | 0.354 | 4.3 |
| DeltaBlue | 642.3 | 60.5 | 387.8 | 0.094 | 0.604 | 6.4 |
| Havlak | 3485.0 | 428.9 | 2347.2 | 0.123 | 0.674 | 5.5 |
| Json | 906.7 | 84.0 | 852.5 | 0.093 | 0.940 | 10.2 |
| List | 1085.3 | 243.5 | 612.0 | 0.224 | 0.564 | 2.5 |
| Mandelbrot | 1374.9 | 50.7 | 90.5 | 0.037 | 0.066 | 1.8 |
| NBody | 1948.3 | 65.1 | 327.5 | 0.033 | 0.168 | 5.0 |
| Permute | 2108.4 | 141.1 | 722.7 | 0.067 | 0.343 | 5.1 |
| Queens | 1002.2 | 83.1 | 303.1 | 0.083 | 0.302 | 3.6 |
| Richards | 4304.2 | 178.6 | 1922.9 | 0.041 | 0.447 | 10.8 |
| Sieve | 2169.2 | 120.4 | 617.0 | 0.055 | 0.284 | 5.1 |
| Storage | 1760.8 | 228.2 | 1126.8 | 0.130 | 0.640 | 4.9 |
| Towers | 1413.7 | 181.4 | 499.6 | 0.128 | 0.353 | 2.8 |
| **geomean** | | | | **0.073** | **0.368** | **5.1** |

**Lucifer is 5x behind PyPy even on AWFY**, which is the code its tier
compiles completely. PyPy runs AWFY 13.7x faster than CPython. The
Smalltalk VM's 0.040 is in the same range, though its denominator is the
GIL build.

Two AWFY rows were counted the same way as the loops in §2, pinned, with
instructions per `bounce()` call and per Richards `benchmark()`:

| | CPython 3.14 | CPython 3.14t | PyPy | Lucifer | Lucifer ÷ PyPy |
|---|---|---|---|---|---|
| Bounce: instructions per `bounce()` | 1976 | 2257 | 70 | 627 | 9.0 |
| Bounce: cycles per `bounce()` | 1006 | 1220 | 28 | 385 | 13.8 |
| Richards: Minstr per `benchmark()` | 429 | 479 | 21.8 | 157 | 7.2 |

On Bounce, 80% of Lucifer's instructions are compiled code. Its
`-X lucifer_jit_stats` counts, per `bounce()` call, show what that code
executes:
- 5.3 increfs and 7.8 decrefs in line. The emitted sequences are 10 and 13
  instructions on the owner's path, so about 155 instructions.
- 2.7 attribute stores, each taking and releasing the object's lock byte
  with a `lock cmpxchg`, plus 49 instructions per call of critical
  sections on the slow paths.
- 11 liveness checks, 2 polls and 3 guards.
- A C call (`h_dict_lacks`) for the method load.

Refcounts and locks are therefore about 35% of Lucifer's best compiled
code. The rest is checks and guards that PyPy's 70 instructions do not
contain. This is the measured upper bound on what the object model and
free-threading cost a compiled loop (§3): about 1.5x, with no allowance
for refcounts a better optimizer would elide.

## 2. Teardown of three hot loops

Three loops from the hottest user functions of mako, pickle_pure_python and
genshi. Each was rebuilt as a ladder of the real library code: each level
adds one Python operation, and the difference between levels is that
operation's cost.

How the counts were taken:
- **Instructions and cycles.** `perf stat -e instructions:u,cycles:u`,
  pinned to one core of a core lease.
- **Steady state.** Each count is a process difference: N reps against
  N+K reps after the same warm-up, so startup and compilation cancel.
- **Rounds.** Five rounds with the runtimes alternating; the median is
  kept. Rounds agree to within 1%.
- **What each runtime does.**
  - CPython: `perf record` by symbol.
  - PyPy: the optimized trace (`PYPYLOG=jit-log-opt`).
  - Lucifer: the SSA after register allocation (`LUCIFER_JIT_DUMP`), the
    per-unit counts of `-X lucifer_jit_stats`, the inline decisions of
    `LUCIFER_JIT_DEBUG`, and `perf record` by symbol.

### 2.1 mako: `render_render_table`'s inner loop

The generated template code. `__M_writer` is `collections.deque().append`,
and `filters.html_escape` is mako's `legacy_html_escape` (five
`str.replace` calls, since the benchmark hides markupsafe):

```python
for col in row:                                   # row is a range(150)
    __M_writer('            <td>')
    __M_writer(filters.html_escape(str(col)))
    __M_writer('</td>\n')
```

Instructions per cell:

| operation | CPython 3.14 | CPython 3.14t | PyPy | Lucifer | Lucifer ÷ 3.14t |
|---|---|---|---|---|---|
| `for col in row` | 75 | 89 | 14 | 21 | 0.24 |
| `__M_writer('<td>')` (`deque.append`) | 165 | 189 | 16 | 383 | 2.0 |
| `str(col)` | 819 | 816 | 216 | 1149 | 1.4 |
| `filters.html_escape(s)` | 2884 | 2948 | 970 | 12059 | 4.1 |
| `__M_writer('</td>\n')` | 166 | 189 | 54 | 383 | 2.0 |
| **cell** | 4274 | 4421 | 1285 | 14377 | 3.3 |
| cycles per cell | 1718 | 1944 | 646 | 6612 | 3.4 |

What each runtime does, per cell:

- **CPython.**
  - 46% of its instructions are the eval loop: specialized attribute
    loads, method-descriptor calls and the frame push for `html_escape`.
  - 50% is C work: `replace` and `ucs1lib_fastsearch` (983),
    `deque_append` (263), `long_to_decimal_string` (137) and the
    allocator.
- **PyPy.** One loop trace of 80 operations covers `l4` and an inlined
  `legacy_html_escape`.
  - `deque.append` is 6 operations: an array store, two field stores and
    a bounds guard.
  - `str(col)` is one residual call (`ll_int2dec`).
  - Each `str.replace` is one residual call into RPython's
    `replace_count` plus 4 operations.
  - The result string is one inline bump allocation (`new_with_vtable`).
  - Of its 1285 instructions, 609 are RPython string work, 331 are GC
    (mostly copying the strings the deque keeps out of the nursery), and
    281 are the trace itself.
- **Lucifer.** The loop compiles as `l4@8`, a 16.6 KB unit.
  - The loop step is in line (`RITER_NEXT`, the small-int cache).
  - **`deque.append` is inlined but stays three C calls**:
    - two `h_slot_load` for `self._ring` and `self._maxlen`, each an atomic
      try-incref in C (`lu_try_xget_ref`), where a field load would do;
    - an `h_prim_call` of `ring_push`;
    - one incref, four decrefs, six guards, a poll and a liveness check.
  - **`html_escape` is not inlined.** Its first `str.replace` uses up the
    one call an inlined frame may make (`opaque_call`). The second is "a
    call it cannot inline", so the whole function stays a call
    (`call_refused`).
  - Its own 6.5 KB unit makes five `h_call`s into the **19 KB
    `str.replace` unit**. `str.replace` is declared
    `def replace(self, *args, **kwargs)`, so each call does this:
    1. `lu_jit_bind` builds an args tuple and an empty kwargs dict in C
       (989 instructions per cell);
    2. `size(args)`, `ns_items(kwargs)` and `_str_arg` twice run as
       primitive calls;
    3. the work itself, `prim_str_replace`, `find_in` and `memmem`, takes
       about 170 instructions.
  - Each call costs about 2,400 instructions, against CPython's 590 and
    PyPy's 194.

Where Lucifer's 14,377 instructions go, by symbol class:

| class | CPython 3.14t | PyPy | Lucifer |
|---|---|---|---|
| eval loop / compiled code | 2047 | 281 | 3569 |
| call protocol (`lu_jit_bind`, `h_call`, frame entry) | 0 | 0 | 1070 |
| allocation and free (incl. libc `mem*`) | 290 | 59 | 4941 |
| refcount out of line (`_Py_Dealloc`, `lu_jit_decref`, merges) | 65 | 0 | 758 |
| locks (`PyCriticalSection_*`) | 71 | 0 | 810 |
| GC | 0 | 331 | 0 |
| helpers (`h_prim_call`, `h_slot_load`, …) | 0 | 0 | 1616 |
| builtin work (C / RPython) | 1948 | 609 | 1614 |

Lucifer's builtin work (1,614) is no more than CPython's (1,948). Its
excess is everything around that work: 9,956 instructions per cell, of which 9,111 (92%)
is `html_escape`.

### 2.2 pickle_pure_python: `_Pickler.save`

`_batch_setitems` runs `save(k); save(v)` over the dict's items. Each
`save` of a str or int runs these lines (CPython 3.14's `pickle.py`):

```python
self.framer.commit_frame()                     # save
pid = self.persistent_id(obj)
x = self.memo.get(id(obj))
reduce = getattr(self, "reducer_override", _NoValue)
f = self.dispatch.get(type(obj)); f(self, obj)
encoded = obj.encode('utf-8', 'surrogatepass') # save_str
self.write(SHORT_BINUNICODE + pack("<B", n) + encoded)
self.write(self.put(idx))                      # memoize
self.memo[id(obj)] = idx, obj
```

Each line was measured alone, over the str and int keys and values of the
benchmark's `DICT`, on a pure-Python `_Pickler` with framing started. The
last row is the whole `save` on the same objects. Instructions per
execution:

| line | CPython 3.14 | CPython 3.14t | PyPy | Lucifer | Lucifer ÷ 3.14t |
|---|---|---|---|---|---|
| `self.framer.commit_frame()` | 957 | 1094 | 4 | 3738 | 3.4 |
| `self.persistent_id(obj)` | 351 | 395 | 0 | 19 | 0.05 |
| `self.memo.get(id(obj))` | 779 | 775 | 423 | 1194 | 1.5 |
| `getattr(self, "reducer_override", _NoValue)` | 580 | 705 | 0 | 543 | 0.77 |
| `self.dispatch.get(type(obj))` | 497 | 867 | 92 | 1008 | 1.2 |
| `obj.encode('utf-8', 'surrogatepass')` | 776 | 771 | † | 10189 | 13 |
| `pack("<B", n)` | 745 | 755 | † | 25736 | **34** |
| `SHORT_BINUNICODE + header + encoded` | 1097 | 1114 | † | 714 | 0.64 |
| `self.write(data)` (`_Framer.write` → `BytesIO.write`) | 894 | 1035 | 53 | 7951 | 7.7 |
| `self.memo[id(obj)] = idx, obj` | 1185 | 1238 | 601 | 1339 | 1.1 |
| **whole `save(obj)`** | 10822 | 12004 | 653 | 56019 | 4.7 |
| cycles, whole `save` | 4507 | 5085 | 223 | 34229 | 6.7 |

† PyPy deletes a pure operation whose result is unused. In context these
operations are inside the 653.

What each runtime does:

- **CPython.**
  - 58% of its instructions are the eval loop and frame push. The `save`
    path is four Python calls: `save`, `save_str`, `memoize` and `put`.
  - `struct.pack`, `BytesIO.write`/`tell` and `str.encode` are C. Each
    costs about 750–1,000 instructions, call included.
- **PyPy.**
  - One trace of 205 operations covers `save`, `commit_frame`,
    `persistent_id`, `save_str`, `write`, `memoize` and `put`.
  - It makes 8 residual calls: dict lookups, `identityhash` for `id()`,
    a dict store and a string-builder append.
  - PyPy's `struct` is interpreter-level RPython whose format loop is
    `@jit.unroll_safe`, so `pack("<B", n)` with a constant format folds to
    a byte store.
  - PyPy's own `pickle.py` writes frames into a `BytesBuilder`, not a
    `BytesIO`.
  - So PyPy's pickle gets its speed from library code written for its JIT,
    as well as from the JIT itself.
- **Lucifer.** Every line that CPython runs in C runs here as generic
  Python:
  - `pack` is `_struct(format)`, then `Struct.pack(*args)` and `_pack`,
    which makes a `bytearray`, zips the items and calls `_pack_item` →
    `_int` (`isinstance`, range checks, `code in 'qQnNP'`). Then come
    `int.to_bytes`, a slice store and `bytes(out)`.
  - `BytesIO.write` does `isinstance`, then `with memoryview(b)`, the
    `closed` property through `_IOBase`, `len()` and a slice store into a
    `bytearray`.
  - `commit_frame` tests a `BytesIO`'s truth and calls its `tell`.
- **Lucifer's inliner.** Of the 464 inline refusals in this run, 289 are
  "a call it cannot inline", 93 the inline budget and 76 "its inlined
  copies kept leaving".

Where Lucifer's 56,019 instructions go:

| class | CPython 3.14t | PyPy | Lucifer |
|---|---|---|---|
| eval loop / compiled code | 6933 | 327 | 13878 (kernel units 9270: `Struct._pack`, `BytesIO.write`, `_zip_new2`, `property.__get__`…) |
| call protocol | 0 | 0 | 2859 |
| lookups (`type_lookup`, `probe_keys`, `lu_getattr_native`) | 946 | 159 | 4946 |
| allocation and free (incl. libc `mem*`) | 795 | 87 | 14425 |
| refcount out of line | 329 | 0 | 1776 |
| locks (`PyCriticalSection_*`) | 227 | 0 | 3067 |
| helpers and builtin work | 2776 | 71 | 15029 |

Root causes of the 44,015 instructions over CPython, weighted by how often
each line runs:

| line | excess per `save` | share |
|---|---|---|
| `pack` | 25,000 | 57% |
| `BytesIO.write` (1.7 per save) | 11,800 | 27% |
| `str.encode` (0.7 per save) | 6,700 | 15% |
| `commit_frame` | 2,600 | 6% |
| the rest | about −0.6k | net |

All of it is `struct`, `io` and codecs written as Python interpreters of
their own inputs (category c).

### 2.3 genshi: `Template._flatten`

The hottest user function is a generator at the centre of a five-generator
pipeline. Its loop, as genshi's text template drives it for a table cell
(events TEXT, EXPR, TEXT):

```python
for kind, data, pos in stream:          # stream: the ForDirective generator
    if kind is START and data[1]: ...
    elif kind is EXPR:
        result = _eval_expr(data, ctxt, vars)   # Expression.evaluate → eval(code, globals(), {...})
        if result is not None:
            if isinstance(result, str): yield TEXT, result, pos
            elif isinstance(result, numeric_types): yield TEXT, number_conv(result), pos
    elif kind is SUB: ...
    elif kind is EXEC: ...
    else: yield kind, data, pos
```

The ladder drives it from a generator that does what the two
`ForDirective`s do: a `ctxt.push({'c': c})` per cell, the three events,
then a pop. It uses genshi's real `Context`, `Expression` and
`_eval_expr`. Instructions per cell:

| step | CPython 3.14 | CPython 3.14t | PyPy | Lucifer | Lucifer ÷ 3.14t |
|---|---|---|---|---|---|
| generators: source → `_flatten` → consumer, 3 events | 3709 | 4779 | 1079 | 6653 | 1.4 |
| the four `kind is …` tests | 1093 | 1242 | 273 | 211 | 0.17 |
| `_eval_expr(data, ctxt, vars)` | 9588 | 10885 | 1101 | 37966 | **3.5** |
| two `isinstance`s, `number_conv`, `yield TEXT, …` | 2059 | 2203 | 415 | 2171 | 1.0 |
| **cell** | 16451 | 19109 | 2868 | 47001 | 2.5 |
| cycles per cell | 7471 | 8910 | 1409 | 28868 | 3.2 |

The `_eval_expr` call chain:
1. `_eval_expr` → `Expression.evaluate`.
2. `self._globals(data)`, a classmethod call that builds a five-entry
   dict.
3. `eval(self.code, globals, {'__data__': data})`.
4. The eval'd code calls `_lookup_name(__data__, 'c')`, another
   classmethod.
5. That calls `Context.get`, which iterates a deque of frames.

What each runtime does:

- **PyPy.**
  - One trace of about 370 operations covers `g3` and the source
    generator, both resumed in line.
  - It also covers `_eval_expr`, `evaluate`, `globals`, the eval'd
    `<Expression 'c'>` code, `lookup_name` and `get`.
  - It makes 10 residual calls (dict lookups, `int2dec`) and 18
    allocations, all inline bumps.
  - `_eval_expr` costs 1,101 instructions.
- **Lucifer.**
  - The two generators are separate units resumed through `lu_jit_resume`
    (1.1k instructions per cell).
  - **The eval'd expression never compiles.** `jit.c` compiles only
    `CO_OPTIMIZED` code, and eval-mode code is not. It runs in the
    interpreter on every cell: 3.7k instructions.
  - `classmethod` access on the class (`self._globals`, `cls.lookup_name`)
    goes through the kernel's `type.__getattribute__` → `_type_getattr` →
    `getattr_hook` → `classmethod.__get__`, with `type_lookup` in C:
    6.4k instructions of lookups.
  - `evaluate`, `globals`, `eval` (with `unpack` for its arguments),
    `lookup_name`, `Context.get` and `deque.__iter__` are separate units
    reached through `h_call`: 4.9k of call protocol.
  - The two dicts per evaluation are GC-tracked. Lucifer's trial-deletion
    collector runs often enough to cost 3.6k instructions per cell.
    CPython's collector costs 7.

Where Lucifer's 47,001 instructions go:

| class | CPython 3.14t | PyPy | Lucifer |
|---|---|---|---|
| eval loop / compiled code | 8696 | 2189 | 13294 (interpreter 3690) |
| call protocol | 22 | 1 | 4925 |
| lookups | 2476 | 472 | 6350 |
| allocation and free | 1773 | 126 | 6043 |
| refcount out of line | 1041 | 0 | 1380 |
| cycle collector | 7 | 5 | 3631 |
| locks | 229 | 0 | 1785 |
| helpers and builtin work | 4865 | 76 | 9593 |

### 2.4 Categories per operation

Each operation's excess over CPython 3.14t, classified by root cause:
- **a**: the C-API object model.
- **b**: free-threading.
- **c**: stdlib or builtins written as Python where CPython has C.
- **d**: a JIT limitation.
- **e**: other.

| operation | excess | category | the mechanism |
|---|---|---|---|
| mako `html_escape` (5× `str.replace`) | 9,111 | d | not inlined (`opaque_call`). Each `str.replace` is an `h_call` into a separate unit whose `*args`/`**kwargs` are built in C (`lu_jit_bind`) and then parsed in Python. The work is ~170 instructions of 2,400 |
| mako `deque.append` ×3 | 582 | d (b in part) | `__slots__` loads as C calls (`h_slot_load`'s atomic try-incref), `ring_push` as a primitive call, 5 refcount ops per append |
| mako `str(col)` | 333 | d | `_new1` → `to_str` → `__str__` → `__repr__` inlined, ending in 2 primitive calls and a `TRUTH` |
| pickle `pack` | 25,000 | c | `_struct` is a format interpreter in Python. CPython runs C, and PyPy runs RPython unrolled on the constant format |
| pickle `BytesIO.write` | 11,800 | c | `_pyio`-style `BytesIO`: a memoryview context manager, a property, a slice store |
| pickle `str.encode` | 6,700 | c | kernel codec path |
| pickle `commit_frame` | 2,600 | c, d | `BytesIO` truth and `tell` in Python, reached through calls |
| genshi `_eval_expr`: calls | ~9,000 | d | `evaluate`/`globals`/`eval`/`lookup_name`/`get` not inlined. `eval` parses its arguments in Python (`unpack`) |
| genshi `_eval_expr`: class-level lookups | ~4,000 | d | classmethod access through kernel `type.__getattribute__`, not a cached load |
| genshi `_eval_expr`: eval'd code | ~4,000 | d | `CO_OPTIMIZED` gate: interpreted on every call |
| genshi `_eval_expr`: alloc, refcount, locks | ~6,000 | d | the dicts and frames of the non-inlined calls, which escape analysis cannot see across the call |
| genshi `_eval_expr`: cycle collections | 3,600 | e | collection frequency, driven by GC-tracked temporaries |
| genshi `deque` iteration in `Context.get` | ~1,500 | c | `_DequeIterator` in Python |
| genshi generator resume | 1,874 | d | `lu_jit_resume` per event; the generators are not resumed in line |

## 3. Root causes and what they cost

Each loop's excess is split by the table above. The three loops are then
weighted equally, so that no single loop dominates:

| category | mako | pickle | genshi | mean | fundamental to compatibility? | end-state if fixed |
|---|---|---|---|---|---|---|
| (d) JIT limitation | ~97% | ~5% | ~80% | **~60%** | no | mako ~2.5–3k instructions per cell (0.6–0.7x CPython); genshi ~8–10k (0.4–0.5x) |
| (c) stdlib in Python | ~2% | ~95% | ~5% | **~35%** | no | pickle ~12k per `save` (CPython parity) with C accelerators. With (d) fixed too and the library written for the compiler, PyPy's 0.65k shows the room |
| (e) other | 0 | 0 | ~12% | ~4% | no | cycle-collection frequency, a policy |
| (b) free-threading | ~1% | <1% | ~2% | ~1% | yes | — |
| (a) C-API object model | ~0 | ~0 | ~0 | ~0 | yes | — |

**(a) and (b) are real but small, and they cap the ceiling rather than
explain the gap.** Measured where they act alone:
- **Free-threading in an interpreter.**
  - CPython 3.14t executes +3% (mako), +11% (pickle) and +16% (genshi)
    instructions over the GIL build on the same loops.
  - It is 12% slower over the 18 programs.
  - The cost is in thread-safe dict lookups, critical sections, biased
    refcounts and the try-incref protocol.
- **Refcounts, the allocator and locks in CPython's own profile.** These
  are 10%, 11% and 16% of its instructions in the three loops. That
  undercounts, since much of the work is inline in the eval loop.
- **Against PyPy's memory model.**
  - PyPy allocates with an inline bump of about 5 instructions and pays
    nothing to drop a reference.
  - It pays in GC copying when objects survive: 331 of mako's 1285
    instructions per cell, 26%, because the deque keeps every string.
  - In Lucifer's best compiled code (AWFY Bounce, §1.1), inline refcounts
    and lock bytes are about 35% of the instructions. Removing all of them
    would give about 1.5x. The other 6x to PyPy is checks, guards and
    calls a better optimizer removes.
  - Allocation-heavy code adds malloc, header initialization and eager
    frees, against PyPy's bump and nursery. The estimate for the
    compatible model's total cost against PyPy's is 1.3–2x on steady-state
    code. That is an estimate from these shares, not an end-to-end
    measurement.
- **What is fundamental.** The C-API model and free-threading are what
  make Lucifer compatible. They put the ceiling below PyPy's, not below
  CPython's.
- **Lucifer's inflated a/b costs are not a/b.** Lucifer spends 3–15x
  CPython's instructions on allocation, refcounts and locks in these loops
  (15x on mako, 14x on pickle, 3x on genshi without its cycle
  collections). Almost all of that comes from (c) and (d) code making
  temporaries CPython's C never makes:
  - an args tuple and kwargs dict per `str.replace`;
  - a `bytearray`, zip, tuples and a memoryview per `pack` and `write`;
  - two dicts per `eval`.

**(c) is a design decision that the data contradicts as built.**
- **DESIGN 4.4** puts the builtins in Python and says the JIT will make
  them fast.
- **PyPy, after 15 years of JIT work, keeps the same modules at
  interpreter level** (RPython, seen through by its JIT): `struct` with
  unrolled format loops, `_io`, `_codecs`, `_collections`, `_sre`.
- **Where PyPy's library is Python and CPython's is C** (asyncio), PyPy
  loses to CPython. Lucifer's Python-written `struct`, `BytesIO` and
  codecs are 8–34x CPython's C per call.
- **What is left of the bet.** "Kernel in Python, JIT makes it fast"
  holds only for thin kernel functions over primitives that the JIT can
  inline and fold. It does not hold for algorithms that interpret their
  inputs, unless the JIT specializes them on constant inputs, as PyPy
  does with `unroll_safe` and Lucifer's `_sre` does by generating code.

**(d) is the largest category, and it is architectural, not tuning.**
- **The three loops share four mechanisms:**
  1. **Function units joined by a generic call.** A call the inliner
     refuses goes through `h_call` → `lu_kernel_target` →
     `call_function`/`lu_jit_bind` → frame push → unit entry.
     - This costs 1.1–5k instructions per loop iteration here.
     - Star parameters are materialized in C, so a kernel function
       declared `(*args, **kwargs)` allocates a tuple and a dict per
       call.
  2. **An inliner that stops at the second call.**
     - `opaque_call` allows an inlined frame one non-inlined call, at
       depth ≤ 2 (3 in kernel code), and none with a loop after it.
     - Refusals for "a call it cannot inline": mako 135 of 139, pickle
       289 of 464, genshi 179 of 201. The rest are the inline budget,
       "its inlined copies kept leaving" and recursion.
     - Real library functions make several calls, so the inliner stops
       one level in, and the kernel underneath is never customized.
  3. **Code it never compiles:** eval/exec/module code (the
     `CO_OPTIMIZED` gate), and generators whose bodies run Python.
  4. **Class-level and slot attribute paths in C or in kernel Python**
     (`h_slot_load`, `type.__getattribute__` → `getattr_hook`) instead
     of cached loads.
- **Kernel customization stages 1–3 (luc-zelv, merged 11-10) address
  these piecewise**, with operator arms and fixed-arity twins for 13 of
  the 130 star-argument kernel functions (`str()`, `int()`, `min`, `max`,
  `sum`, `startswith` and others). The loops here show the general case
  is still open: `str.replace` has no twin, and the inliner rule that
  stops at the second call is unchanged.

## 4. The levers that give multi-x

Each lever names the evidence, the expected effect on the band and the
cost. "Agent-days" are at the repository's measured pace: the repository is
9 days old and holds an 18.5k-line JIT, and kernel customization stages
1–3 took one day from design to merge.

### Lever 1: compile the hot region, not the function

**What it is.** Take the hot loop plus everything it calls as one
compilation region, user, library and kernel code alike, and fold it on
the types and constants that flow in. PyPy's traces do this, and so do
Truffle-style partial evaluators. It does not need a tracing tier. The
SSA builder already inlines, folds and rebuilds inlined frames at exits;
what stops it is policy and a few missing frame cases:
- **Drop the `opaque_call` limits** (one call per inlined frame, depth
  2–3, no loop after). Since luc-hez4, an inlined frame can call through
  `push_frames`. Charge the budget in IR instructions after folding, as
  kernel-customization §3 proposes.
- **Make a call the inliner declines a direct unit-to-unit call.** It
  should bind positional, star and keyword arguments at compile time,
  never build a tuple or dict for a callee's `*args`/`**kwargs`, and
  enter the callee's unit without `call_function`.
- **Compile non-`CO_OPTIMIZED` code** (eval, exec, class and module
  bodies). `LOAD_NAME` on a known dict is a guarded dict load.
- **Resume generators in line when their bodies run Python**
  (the first half of luc-s4lm). Keep them virtual where they do not
  escape (the second half).
- **Give every kernel function with `*args`/`**kwargs` a fixed-arity
  twin**, generated rather than written by hand (§5 of the
  customization design, for all 130).
- **Load slots and class attributes inline.** Read `__slots__` with an
  inline load and an owner check, not a C try-incref. Cache class-level
  and classmethod attributes by type version.

**Evidence.**
- 62–97% of inline refusals are this rule.
- PyPy's single traces span 8–11 functions in pickle and genshi.
- On mako, `html_escape` is 12,059 instructions in Lucifer and 970 in
  PyPy.
- The call protocol alone is 1.1–5k per iteration.

**Expected.** These are estimates from the per-operation tables.
- mako 14.4k → ~2.5–3k instructions per cell (5x, 0.6–0.7x CPython);
- genshi 47k → ~8–10k (5x);
- chameleon, html5lib and sqlglot, the template and parser band,
  similarly.

**Cost.** The largest of the three: the inliner's policy and budget,
compile time (cli.black is the canary), deopt across deep chains, and
deep-inline correctness under `deopt:N` stress. About 5–10 agent-days in
stages, each gated on the 18 programs.

### Lever 2: accelerators as C or as code written for the compiler

**What it is.** Choose, per module, one of two forms:
- **C**, ported from CPython (`_struct.c`, `Modules/_io/bytesio.c` and
  `stringio.c`, the utf-8/latin-1/ascii codecs, `_asynciomodule.c`'s Task
  and Future, `_sre`). The luc-clibs pipeline already builds C modules
  from CPython source.
- **Kernel Python that lever 1 can fold:** `Struct` compiled per format
  into a generated function, as `_sre` does for patterns; BytesIO as
  primitive buffer operations, not a memoryview and a slice store.

**Evidence.**
- `pack` costs 34x CPython's C, `encode` 13x and `BytesIO.write` 7.7x.
- pickle_pure_python's whole gap is these lines.
- PyPy keeps each of them at interpreter level, and loses to CPython on
  the one it leaves in Python (asyncio).
- corpus-gap §4 measured asyncio at 10x CPython's cost and the regex
  fallback at 9–200x.

**Expected.**
- pickle and unpickle ~7x → ~1x with C, and below 1x with lever 1 and
  code written for the compiler;
- the async_tree family ~4–5x → ~1–1.5x;
- docutils, sphinx, pygments and markdown, through `_sre`, per
  corpus-gap §4.

**Cost.** Each module is a port of known C: about 0.5–2 agent-days per
module, and asyncio's Task and Future the largest. This reverses part of
the owner's 11-10 split, which keeps struct and io in Python as JIT
targets. That split pays only after lever 1 lands, and even then only
for modules rewritten for the compiler. As written today they are generic
interpreters.

### Lever 3 (lever 1's companion): no temporary objects across inlined kernel code

**What it is.** Escape analysis and refcount elision over the region that
lever 1 builds:
- tuples, dicts, bound methods, iterators and memoryviews that kernel code
  makes and drops stay virtual;
- inc/dec pairs on borrowed values go;
- the allocation that remains takes the inline fast path.

Lucifer already makes tuples, instances and loop iterators virtual inside
one unit (DESIGN 6.2 step 3). Lever 1 makes the unit cover the code where
the temporaries live.

**Evidence.**
- Allocation and free are 34% of Lucifer's instructions on mako, 26% on
  pickle and 13% on genshi, against CPython's 7%, 7% and 9%.
- Out-of-line refcounts and locks are another 7–11%.
- Inline refcount operations in compiled code are about 10 instructions
  each: 5 per `deque.append`.

**Expected.** About 1.5x on top of lever 1. On its own, without lever 1,
it is a 10–20% lever, because the temporaries are made in other units.

**Cost.** About 3–5 agent-days. It extends the existing scalar
replacement to dicts and memoryviews and through inlined kernel frames.

### Lever 4: the optimizer's middle, for the code it already compiles

**What it is.** Lucifer is 5x behind PyPy on AWFY, the code its tier
compiles in full. That gap is optimizer quality, not coverage. On Bounce,
one `bounce()` is 627 instructions against PyPy's 70. Per call, the
compiled code runs:
- 11 liveness checks after calls that cannot kill the unit;
- 13 inline reference counts on values whose ownership is known;
- 2.7 lock-byte round trips on objects the loop's thread owns and has not
  published;
- a C call to prove the instance dict lacks a method name, which a type
  version and a shape guard already prove.

**Evidence.** Bounce (§1.1). The Smalltalk VM's tier reached 0.04 of
CPython on AWFY largely through fact retention, guard and check
elimination, and cheap calls.

**Expected.** 2–3x on AWFY-shaped code. It is smaller on the band until
levers 1 and 2 have removed the calls that dominate there.

**Cost.** Several independent passes of about 1–3 agent-days each, each
measured on AWFY instructions.

**What is not a lever.**
- **Faster refcounts or locks on their own.** In CPython they are under
  16% of the work, and Lucifer's excess there is caused by (c) and (d).
- **Containers' hot operations as intrinsics.** `dict.get`, a dict
  store, `list.append` and `len` are already at parity or better. The
  exception is `deque.append`, which is lever 1's slot loads.
- **A tracing tier beside the SSA tier.** The Smalltalk VM measured and
  rejected trace tiers, and the property that matters, region
  compilation with folding, is available in the SSA builder.
- **Warm-up (code cache, snapshot)** stays corpus-gap lever 3. It is
  worth up to 2.5x on one-shot runs: compile and import are 25–60% of a
  1-loop run. It does nothing for steady state, and PyPy's CLI numbers
  show that a fast JIT without a cache still loses one-shot runs.

## 5. Verdict

**Is "CPython-compatible with the C-API, and much faster than CPython"
reachable with the current architecture? Not with the current JIT
architecture. The object model is not what blocks it.**
- **Python's semantics allow it.** PyPy is 2.2x faster than CPython on
  this band at steady state, and 17x on pickle.
- **The C-API object model and free-threading allow it, at a price.**
  CPython 3.14t carries both and is 4.7x faster than Lucifer. They cost
  an interpreter 3–16%, and about 35% of Lucifer's best compiled loop
  (Bounce). Against PyPy's memory model, the estimate is 1.3–2x on
  steady-state code. That makes something below PyPy's level, about
  1.5–2x CPython on the band, the realistic ceiling for a compatible
  runtime. It is not 5–10x. On AWFY-shaped code, where PyPy reaches
  13.7x CPython, a compatible runtime can plausibly reach 5–10x.
- **The JIT as built cannot get there by incremental cuts.**
  - It compiles functions and joins them with a generic call. Its
    inliner stops at an inlined frame's second call. It never compiles
    eval'd code.
  - It runs a kernel that emulates CPython's C argument parsing in
    Python, and accelerators that are generic interpreters of their
    inputs.
  - On the band, that makes every builtin a slower CPython: Lucifer
    executes 2.5–4.7x CPython's instructions in the three hot loops,
    and 11–86x PyPy's.
  - Where it does compile everything (AWFY), it is still 5x behind PyPy,
    because of checks, refcounts and lock bytes the optimizer does not
    remove.
- **What must change:**
  - The unit of compilation becomes the hot region, through kernel and
    library code (lever 1, with lever 3 as its companion).
  - The accelerators become C or code written for that compiler
    (lever 2).
  - The optimizer removes what its own compiled loops do not need
    (lever 4).
  - With both, the measured per-operation numbers put mako, genshi and
    pickle at 0.4–0.7x CPython's instructions. Template engines and
    pickle would be 1.5–2.5x faster than CPython. The async and regex
    families would be at parity or better once their C lands.

## 6. Method, caveats, reproduction

- **Builds.**
  - Lucifer at master bda3952, built with `make all`.
  - PyPy 3.11 v7.3.23, from the linux64 tarball on downloads.python.org,
    with its sha256 checked against pypy.org.
  - CPython 3.14.10 and 3.14.10t, the oracles.
- **Corpus timing (§1).**
  - All four runtimes ran in one exclusive session on the bench host,
    unpinned as the daily is, through `tools/bench/corpus`'s
    `pyperf_run` and `measure_cli`.
  - The loop counts are `results/status-reference.json`'s.
  - Steady state is `-w 20 -n 5`, best value.
  - PyPy runs its own 3.11 stdlib. Its `pickle.py` uses a `BytesBuilder`
    for frames, and its asyncio has no C core. So PyPy's numbers measure
    PyPy's library as well as its JIT.
- **AWFY (§1.1).**
  - `tools/bench/awfy` for PyPy and Lucifer, in one exclusive session.
    Every row passed its verification.
  - The CPython column is the 11-11 daily's, from the same harness on
    the same host.
  - The Bounce and Richards counts call the benchmark's `benchmark()`
    N and N+K times after a warm-up, pinned, three rounds.
- **Ladders (§2).**
  - `perf stat -e instructions:u,cycles:u` under a core lease, pinned
    with `taskset`.
  - Per-operation counts are process differences between N and N+K reps
    after the same warm-up. Five rounds, runtimes alternating, median
    kept; the rounds agree to within 1%.
  - Cycles in small differences are noisy, and one difference was
    negative. The instruction counts are exact to within 1%.
  - Every ladder ran on all four runtimes. PyPy runs its own 3.11
    `pickle.py`, whose framer differs (see above).
- **Profiles.**
  - `perf record -e instructions:u -c 200003` at two rep counts,
    differenced per symbol.
  - Lucifer names its units in the perf map during warm-up only
    (`sys.activate_stack_trampoline`, then deactivated). Under `-X perf`
    the trampoline hook changes Lucifer's call path (`h_call` takes the
    generic route when `eval_frame` is set): genshi measured 78k
    instructions per cell with the hook and 47k without.
  - The symbol classes are regular expressions over symbol names. Inline
    refcount and lock work inside the eval loop and compiled code is not
    separated out, so those rows undercount for CPython and Lucifer
    alike.
  - Unresolved libc addresses (`mem*`, unverified) are counted with
    allocation.
- **The category shares (§3) are judgements from the per-operation
  mechanisms**, not a symbol-level partition. The end-state estimates are
  estimates.
- **PyPy's dead-code elimination.** PyPy deletes pure operations whose
  results are unused. Its per-line pickle numbers are therefore lower
  bounds, and only its whole-`save` number compares.
- **The ladders are rebuilt loops, not the programs.** Their ratios track
  the programs':
  - mako: 3.3x instructions and 3.4x cycles, against 3.4x program time;
  - genshi: 3.2x cycles, against 4.7x program time (the rest is the other
    pipeline stages);
  - pickle `save`: 6.7x cycles, against 7.1x.
