# Corpus gap, 2026-11-11

Status: measurement. Runtime as of master 79c72a5. Scores from the 11-10 daily.

## 0. Question and answer

The 11-10 daily scores 122 programs at a geomean of 2.89x CPython. The 81
programs between 2x and 10x set that geomean. They have flat profiles over
large code bases. This study asks where their time goes and which structural
levers would move the whole band. Every number below is measured.

The answer, in short:

- **The interpreter is not where the time goes.** In the scored phase,
  interpreted user code is 1.5% (median) of the band's cycles. Interpreted
  kernel code is 0.0%. Kernel code is already compiled.
- **JIT compile time is not where it goes either.** It is 0.5% (median).
- **The time goes to native runtime work done for compiled code**: 69% of the
  band's cycles at the median, 45–83% from the 10th to the 90th percentile.
  That covers allocation and GC, refcounts and locks, calls and lookups, and a
  long tail of small helpers. The tail is flat: no
  single symbol averages above 3%.
- **Instruction counts track cycles.** Lucifer executes 1.6–11x CPython's
  instructions for 2.3–9.3x its cycles. The gap is volume of work, not stalls.
  Much of that volume sits in the kernel. On every sampled program but
  coverage, the kernel's own time alone is at least CPython's whole run.
- **Two subsystems are far behind CPython's C and decide whole families of
  programs.** Lucifer's asyncio costs 10x CPython's at the daily's loop counts.
  Its regex engine costs 9–200x on text-processing patterns.

The candidate levers, with their measured effect on the corpus geomean (2.89):

| lever | corpus geomean | programs gaining >5% | basis |
|---|---|---|---|
| kernel-owned time halved (sensitivity) | 2.41 (−16.6%) | 78 | kernel share 37% median in band |
| import + compile gone (code cache + snapshot), bound | 2.65 (−8.4%) | 27 | profile shares, scored phase |
| asyncio at CPython's cost | 2.69 (−7.1%) | 16 | per-program lucifer/CPython profiles |
| regex engine at CPython's cost | 2.73 (−5.6%) | 17 | per-program lucifer/CPython profiles |
| compile off the critical path, bound | 2.81 (−2.8%) | 19 | profile shares, scored phase |
| O(1) instruction start (`lu_instr_before`) | 2.86 (−1.1%) | 5 | symbol shares |
| 10x lower compile thresholds | +2.0% worse (sample) | — | built and run, §4 |
| cheaper interpreter path for kernel builtins | nil | 0 | interpreted kernel is 0.4% mean |

The levers that move the band are, in order:

1. **Less native work per Python operation**, through kernel customization
   (luc-zelv). It is the only lever that reaches most of the corpus (78
   programs gain over 5%).
2. **asyncio and the regex engine at C-like cost.** Together they are −12%
   on the corpus, from about 33 programs.
3. **A persistent code cache with a startup snapshot.** It is worth up to
   −8.4% on the score, and more on first runs, where compile plus import is
   25–60% of the cycles.

## 1. Method

Eighteen programs spread across the band were measured on the bench host:
sqlglot_v2, html5lib, genshi, mako, chameleon, typing_runtime_protocols,
three async_tree variants, sphinx, docutils, coverage, pickle_pure_python, and
cli.black, pyflakes, pycodestyle, markdown and pygments. Then a profile sweep
covered all 107 corpus programs that the corpus tool runs. AWFY and web are
not in it. The cadence programs and fastapi did not run under the harness, so
101 programs remain.

- **Timing.** `perf stat -e cycles:u,instructions:u`, pinned to one CPU, best
  of two. For each program:
  - startup + imports: the program with `--help` (pyperf's Runner exits after
    the module imports);
  - the 1-loop worker run (`--worker -l 1 -w 0 -n 1`);
  - a steady loop: (T(2K) − T(K)) / K, with K loops taking about a second on
    CPython;
  - a daily-shaped value: the same difference at the daily's own loop count.
    The daily scores the best value after one warmup value, so this is what it
    sees.

  The cli programs are scored as whole runs.
- **Breakdown.** `perf record -g -e cycles:u` with `-X perf`. Each sample is
  classified by its stack.
  - **a.** Interpreter running user or stdlib code: the leaf is
    `eval_frame`, or a trampoline whose nearest Python frame is not compiled.
  - **b.** Interpreter running kernel code: the same, when that frame is
    `<frozen K>` for a `lib/kernel` or `lib/modules` file, or `<sre>`.
  - **c.** Compiled code: a `py::…@N` unit, user or kernel.
  - **d.** JIT compile: any of `compile`, `lu_ssa_build`, `lu_ssa_optimize`,
    `lu_regalloc`, `lu_target_emit` or `lu_jit_deps_publish` on the stack.
  - **e.** Native runtime: any other leaf. It is split into type feedback,
    alloc/gc, refcount/locks, lookup/calls and other.
  - **f.** Import: `_find_and_load` and its kin on the stack.

  A second key, the **owner**, is the code of the nearest Python frame,
  native callees included: program, stdlib, asyncio, kernel, or regex engine
  (`<sre>` and `_sre`). Scored-phase shares are differences of sample counts
  between two loop counts, so startup cancels.
- **CPython side.** The same profiles of CPython 3.14t with `-X perf`, for
  the 38 programs with a regex or asyncio share above 2%. A sample counts as
  regex when an `sre_`/`pattern_` symbol is on its stack, and as asyncio when
  `_asyncio` is or the nearest Python frame is in `asyncio/`.
- **Lever estimate.** A lever acting on a share x, making it r times cheaper,
  turns a program's scored time t into t·(1 − x + x/r). The corpus geomean
  moves by the geomean of those factors over all 122 programs. Programs
  outside the sweep are unchanged.

## 2. Where the time goes

Scored phase (steady loop, or the whole cli run). Shares of lucifer's cycles
are in %. The x columns are ratios to CPython in cycles.

| program | scored x | steady x | one-shot x | interp | compiled | compile | native | import | kernel-owned | regex | asyncio |
|---|---|---|---|---|---|---|---|---|---|---|---|
| sqlglot_v2 | 2.7 | 2.3 | 4.9 | 3 | 30 | 2 | 65 | 0 | 44 | 0 | 0 |
| mako | 3.5 | 3.4 | 5.8 | 0 | 28 | 0 | 72 | 0 | 71 | 0 | 0 |
| async_tree_io | 4.0 | 3.7 | 3.8 | 7 | 20 | 0 | 73 | 0 | 31 | 0 | 54 |
| async_tree | 4.2 | 3.9 | 4.7 | 4 | 21 | 0 | 74 | 0 | 33 | 0 | 58 |
| async_tree_eager_tg | 4.2 | 4.4 | 5.4 | 2 | 26 | 0 | 72 | 0 | 43 | 0 | 45 |
| html5lib | 4.2 | 3.7 | 5.7 | 3 | 43 | 0 | 54 | 0 | 37 | 27 | 0 |
| cli.pyflakes | 4.3 | | 4.3 | 4 | 13 | 12 | 60 | 11 | 43 | 0 | 0 |
| cli.pycodestyle | 4.3 | | 4.3 | 5 | 27 | 11 | 54 | 3 | 27 | 22 | 0 |
| genshi | 4.6 | 4.4 | 5.9 | 2 | 33 | 0 | 65 | 0 | 38 | 5 | 0 |
| sphinx | 6.0 | 6.0 | 6.0 | 2 | 39 | 4 | 53 | 2 | 24 | 46 | 0 |
| cli.black | 6.3 | | 6.3 | 4 | 14 | 16 | 42 | 24 | 27 | 14 | 0 |
| docutils | 6.5 | 6.4 | 6.8 | 1 | 48 | 2 | 50 | 0 | 19 | 59 | 0 |
| chameleon | 6.6 | 6.4 | 7.5 | 0 | 26 | 0 | 74 | 0 | 79 | 6 | 0 |
| typing_runtime_protocols | 6.7 | 5.9 | 7.2 | 0 | 30 | 0 | 70 | 0 | 63 | 0 | 0 |
| pickle_pure_python | 6.9 | 6.8 | 7.5 | 0 | 31 | 0 | 69 | 0 | 83 | 0 | 0 |
| coverage | 6.9 | 6.7 | 7.1 | 19 | 0 | 0 | 81 | 0 | 0 | 0 | 0 |
| cli.markdown | 9.2 | | 9.2 | 4 | 15 | 19 | 28 | 34 | 23 | 37 | 0 |
| cli.pygments | 9.3 | | 9.3 | 7 | 23 | 11 | 50 | 9 | 19 | 53 | 0 |

Across the 75 swept programs in the 2–10x band, the scored-phase shares are
(median / mean):

| category | median | mean |
|---|---|---|
| a interpreter, user | 1.5% | 3.1% |
| b interpreter, kernel | 0.0% | 0.4% |
| c compiled user + kernel | 24.5% | 22.9% |
| d JIT compile | 0.5% | 3.7% |
| e native: alloc/gc | 13.5% | 16.1% |
| e native: refcount/locks | 9.6% | 10.1% |
| e native: lookup/calls | 11.6% | 10.6% |
| e native: other | 28.1% | 27.6% |
| f import | 0.1% | 4.7% |
| e native, all | 68.6% | — |
| owner kernel | 37.3% | 37.5% |
| owner regex engine | 0.1% | 9.5% |
| owner asyncio (22 programs) | 45.1% | 34.5% |

The native share is flat. Averaged over the sample, the top symbols are:

| symbol | share |
|---|---|
| `_mi_page_malloc_zero` | 2.8% |
| JIT stubs without a symbol | 2.3% |
| `lu_instr_before` + `lu_caches_at` | 2.9% |
| `_PyMutex_Unlock` | 1.4% |
| `type_lookup` | 1.3% |
| `Py_DECREF` | 1.3% |
| `lu_obj_free` | 1.2% |
| `h_prim_call` | 1.1% |

Nothing else reaches 1%. On the ten programs profiled on both sides,
lucifer's allocation and GC cycles are 2.6–7.7x CPython's, and its refcount
and lock cycles 2.3–15x (15x on pygments only). The programs' overall ratios
are 3.7–9.3x. Neither cost is far out of proportion: lucifer simply does more
of everything.

**Instructions track cycles.** The instruction ratio to CPython is 0.64–1.22
times the cycle ratio:

| program | instructions x | cycles x |
|---|---|---|
| sqlglot_v2 | 1.6 | 2.3 |
| sphinx | 6.0 | 6.0 |
| docutils | 7.2 | 6.5 |
| markdown | 11.2 | 9.2 |

IPC is lower than CPython's on genshi, chameleon, pickle and async_tree: 1.3–1.6
against 1.9–2.3. That costs those programs 25–40% on top of their instruction
ratio.

**The kernel is the floor.** Kernel-owned time counts only samples whose
nearest Python frame is a kernel function. Kernel code inlined into user units
counts as user, so this is a lower bound. Even so, it alone is 1.0–5.7x
CPython's whole loop on every sampled program except coverage:

| program | kernel time ÷ CPython's whole loop |
|---|---|
| pickle_pure_python | 5.7 |
| chameleon | 5.0 |
| typing_runtime_protocols | 3.7 |
| mako | 2.4 |
| markdown | 2.1 |
| async_tree_eager_tg | 1.9 |
| docutils | 1.2 |
| sqlglot_v2 | 1.0 |

On mako, chameleon and pickle, everything outside the kernel is already
within 1.0–1.4x of CPython's whole loop.

## 3. First iteration against steady state

| program | lucifer startup + imports (Mcycles) | CPython | x | 1-loop run: compile | 1-loop run: import |
|---|---|---|---|---|---|
| sqlglot_v2 | 2232 | 435 | 5.1 | 18% | 26% |
| mako | 2648 | 530 | 5.0 | 17% | 40% |
| async_tree | 2013 | 437 | 4.6 | 7% | 13% |
| html5lib | 2178 | 459 | 4.7 | 15% | 33% |
| genshi | 1930 | 378 | 5.1 | 13% | 21% |
| sphinx | 3729 | 1053 | 3.5 | 11% | 12% |
| docutils | 1932 | 394 | 4.9 | 5% | 2% |
| chameleon | 7026 | 1014 | 6.9 | 14% | 18% |
| typing_runtime_protocols | 1705 | 314 | 5.4 | 17% | 42% |
| pickle_pure_python | 1711 | 335 | 5.1 | 18% | 35% |

- A bare `-c pass` costs 113–130 Mcycles against CPython's 40: 2.8x. With the
  program's imports, startup costs 3.5–6.9x CPython's.
- On the sample, the geomean ratio to CPython is:

  | measure | ratio |
  |---|---|
  | one-shot (the 1-loop run, or the whole cli run) | 6.0x |
  | daily-shaped value | 4.9x |
  | steady state | 4.7x |

- Lucifer's first loop costs 1.1–5.6x its steady loop. For
  typing_runtime_protocols it is 120x, because the loop is a 6-microsecond
  body.
- The daily's warmup value hides most of this. Its scored values are within
  8% of steady state, except html5lib (15%), typing_runtime_protocols (14%)
  and sqlglot_v2 (18%).
- First runs are where compile and import matter. They take 25–60% of a
  1-loop run on most programs, and 30–50% of black and markdown runs.

## 4. The candidate levers

**A cheaper interpreter path for kernel builtins: nil.** Interpreted kernel
code is 0.0% median and 0.4% mean in the band, and at most 4.9% (pygments).
The kernel runs compiled. Its cost is in that compiled code (11% median) and
in the native helpers it calls. That cost belongs under lever 1 below, not
under interpreter speed.

**Lower compile thresholds: worse.** A build with `LU_JIT_THRESHOLD` 100
(from 1000) and `LU_JIT_THRESHOLD_PER_UNIT` 1 (from 4) was run on the 18
programs. Against master:

| measure | geomean | range |
|---|---|---|
| one-shot | +6.6% slower | −3.6% to +24.9% |
| scored values | +2.0% slower | −16.4% (typing_runtime_protocols) to +8.4% |

More compiles of colder code cost more than the earlier speed returns.

**Background compile (luc-f7cn): small.** JIT compile is 0.5% median and 3.7%
mean of the scored phase, and 2.8% of the corpus geomean as a bound (all
compile moved to another core). It is larger for whole-run programs:

| program | compile share of the run |
|---|---|
| isort | 23% |
| markdown | 20% |
| black | 15% |
| sqlite_synth | 29% |
| sqlglot_v2_optimize | 18% |

**A persistent code cache (luc-p7vb) plus a startup snapshot: up to −8.4%.**
That bound removes all compile and import from the scored phase. It falls on
27 programs:

| program | gain |
|---|---|
| concurrent_imap | 85% |
| cli.isort | 72% |
| asyncio_tcp | 69% |
| cli.markdown | 54% |
| python_startup | 45% |
| cli.black | 40% |
| 2to3 | 29% |

pyperf-scored programs mostly keep their imports outside the timed loop, so
the score gains less than users would. The one-shot ratio (6.0x) would gain
the most.

**asyncio at CPython's cost: −7.1%.** The async_tree family spends 31–58% of
its cycles in `asyncio/` code: Task, Future and the event loop, run as
Python. CPython runs Task and Future in `_asyncio`. Lucifer's asyncio cycles
are 10x CPython's at the daily's loop counts (6.1–13.9 per program), and
4.5–5.0x in steady state. At CPython's cost, 16 programs would gain 36–53%.

**The regex engine at CPython's cost: −5.6%.** Lucifer's `_sre` runs
patterns either as generated Python (`<sre>`) or through the generic
`_match` interpreter in `_sre.py`. On docutils, the generic `_match` alone
takes 30% of all cycles. Against CPython's C engine, lucifer's regex cycles
are:

| program | lucifer ÷ CPython |
|---|---|
| argparse_subparsers | 198 |
| json_loads | 140 |
| docutils | 86 |
| chameleon | 55 |
| cli.markdown | 42 |
| cli.pygments | 37 |
| cli.black | 37 |
| genshi | 30 |
| pathlib | 30 |
| sphinx | 27 |
| html5lib | 11 |
| tomli_loads | 9 |
| cli.pycodestyle | 9 |
| regex_v8 | 7.0 |
| regex_effbot | 2.7 |
| regex_dna | 1.6 |

The regex benchmarks' own patterns are near parity. Real programs' patterns,
which fall back to the generic matcher, are not. At CPython's cost, 17
programs would gain:

| program | gain |
|---|---|
| regex_v8 | 74% |
| docutils | 58% |
| cli.pygments | 52% |
| regex_effbot | 50% |
| sphinx | 45% |
| cli.markdown | 36% |
| html5lib | 23% |
| cli.pycodestyle | 20% |

**The instruction-start scan: −1.1%, and a defect worth fixing (luc-621k).**
`lu_instr_before` walks the code from its first unit to find the instruction
before a pc. It runs on every traceback entry, exception unwind, `f_lineno`
read, deopt and interpreted PIC note. It costs:

| program | share |
|---|---|
| coverage | 21% |
| cli.black | 11% |
| sympy | 8% |
| async_tree_eager | 7% |
| coroutines | 6% |

**Coverage runs 1.75x faster with the JIT off (luc-9drr).** Its daily-shaped ratio is
3.9x with `-X lucifer_jit=off` and 6.9x with the JIT on. Under the tracer
nothing compiles (compiled share 0%), but the on-path still pays for
instrumented-opcode lookups (`original_opcode_at`, `lu_base_opcode`,
`lu_instrumented_caches`: 8%) on top of the scan above. On every other
sampled program, the JIT is worth 2–4x: the scored geomean is 13.8x with it
off and 5.3x with it on.

## 5. The levers that move the band

**1. Less native work per Python operation: kernel customization.**
Native runtime work is 69% of the band's cycles at the median, and instructions track
cycles. The gap is the amount of work each Python operation turns into, not
the speed of any one part. The kernel is where that work is decided: 37%
median of the band's cycles is kernel-owned, and that is a lower bound.
Halving kernel-owned time is −16.6% on the corpus geomean (2.89 to 2.41) and
reaches 78 programs. That is a sensitivity, not a measured fix. It is the
lever the kernel-customization design (luc-zelv) pulls: types flowing into
inlined kernel code fold its dispatch away, and the allocations, calls and
refcounts that dispatch causes go with it. No other lever reaches more than
27 programs.

**2. asyncio and the regex engine at C-like cost.** These are measured
against CPython, program by program. They are −7.1% and −5.6%, together
about −12% (2.89 to 2.53), from about 33 programs. Each family gains
30–75%. Either C accelerators from CPython's `_asyncio` and `_sre` or a
customized Python implementation can deliver this. The owner's split
(external-library wrappers in C, the rest in Python as JIT targets) does not
yet place these two. The numbers say they are worth the decision. For the
regex engine, the fallback `_match` path is the part that loses; the
generated-code path is near parity on the regex benchmarks.

**3. A persistent code cache with a startup snapshot.** The bound on the
score is −8.4% (27 programs). The larger effect is on first runs: the
one-shot geomean is 6.0x against 4.7x steady, and compile plus import is
25–60% of a 1-loop run. Background compile alone is a bound of −2.8%. Lower
thresholds made things worse.

## 6. Caveats

- **`-X perf` inflates some runs.** It adds 17–64% to sqlglot_v2, genshi,
  html5lib and black, and nothing to mako. Shares come from those profiles;
  cycle totals come from runs without it.
- **Some estimates are bounds or sensitivities.** The kernel-halving lever
  is a sensitivity. The compile and import levers are upper bounds. The
  asyncio and regex levers use measured per-program cost ratios.
- **Coverage is incomplete.** AWFY, web, the five cadence programs and
  fastapi are not in the sweep, and count as unchanged.
- **Scores here are user-mode cycles on a pinned CPU; the daily's are
  unpinned wall time.** Kernel-mode cycles are under 2% of user-mode on the
  four programs checked, so they do not explain the difference. The sample's
  scored ratios are 0–40% below the daily's. pickle_pure_python is 6.9–8.3x
  here and 16.4x in the daily; the cause is not established. The lever
  percentages are relative, so they do not depend on it.
