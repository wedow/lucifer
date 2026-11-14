# JIT redesign: architecture of record and roadmap

2026-11-12. Written against master at `fba0b79`. Inputs: the ceiling study
(`jit-ceiling-2026-11-11.md`), the corpus-gap study
(`corpus-gap-2026-11-11.md`), the one-call-rule experiment (branch
`luc-region`, `31721c4`), and two external design documents written from a
description of master at `10db560` without access to the code: an execution
engine design (cited as ENGINE) and an adaptive optimization roadmap (cited
as ADAPTIVE). This document checks both against the code, re-measures the
numbers they rest on, and replaces their sequencing.

Round 2 (same date, master at `2dc44d5`, whose JIT sources are those of
`fba0b79`): a third external document (cited as ROUND2) answered how to go
from this plan's end state to PyPy's level under the cp314t ABI. It was
written from the residual-code, PyPy-trace and side-by-side studies of the
three loops (cited as the side-by-side study; not in the tree). Its claims
were checked against the code (appendix C).

Round 3 (same date): the round-2 review treated machinery ROUND2 assumed
and the tree lacks as errors in ROUND2, and set gates well short of its
targets. That is withdrawn. Missing machinery is built, and a layer that
is not a foundation for the end state is replaced. The architecture and
the plan are now `jit-architecture.md`; this document keeps the baselines
(§1), the evidence (§2), the targets (§4), the tests (§5), the risks (§6)
and the checked claims (appendices).

Every number is marked **verified** (measured for this document on
`fba0b79`), **packet** (taken from the studies or the external documents,
not re-measured) or **estimated** (a prediction or a budget nobody has
measured). Method is in appendix B.

## 0. Owner decisions

Open. Each has a recommendation; the roadmap below assumes the
recommendations.

1. **What is the bar?** PyPy's neighbourhood or better on real programs
   under full cp314t compatibility. The targets are ROUND2's per-loop
   figures (§4), gated in instructions per iteration of named loops
   against CPython and PyPy (§1). The 122-program geomean stays the
   headline.
2. **Lazy frames before call-shape binding?** Settled by the
   architecture: a frame is a virtual object from the first slice, and
   binding is evaluated against it. Nothing widens inlining over pushed
   frames.
3. **Build the task-owned activation stack?** ENGINE makes compiled calls
   jumps with continuation records on a task stack, so a task can migrate
   under compiled callers. *Recommend:* no, not now. Migration exists only
   as the two-carrier handoff stress policy, so a pin costs nothing
   measurable today, and DESIGN 5.2 (no stack switching, a park returns to
   the carrier loop) already holds if a park stays an exit. Revisit when a
   real scheduler exists and parks per call are measured on the web tiers.
4. **Private entries.** Part of the call protocol that replaces the direct
   entry (`jit-architecture.md` §4.4), not a late milestone. A compiled
   positional call is 215 instructions against CPython's 488 and PyPy's 98
   (verified).
5. **ADAPTIVE's content proofs and M5–M7** (the proof that `str(int)` has
   no character `html_escape` replaces; adaptive data layout; indexes;
   incremental recomputation). The only evidence offered is the Mako
   benchmark's own input. *Recommend:* off the roadmap. Reopen one when a
   second, unrelated program shows the pattern.
6. **A persistent code cache and a compiled kernel image for one-shot
   runs.** Compile plus import is 25–60% of a first run (packet), and the
   cli suite is the worst row (5.64x, daily). *Recommend:* yes, after the
   replacement (stage S10), with the constraint applied from the first slice: new
   metadata (safepoint tables, binding plans, summaries) holds no raw
   pointers that a constant table could hold.
7. **Kernel code written for the compiler.** Kernel modules stay as
   written and the evaluator specializes them on their constant inputs;
   per-format generated packers are not built. Small primitives under
   kernel Python and kernel code rewritten so the evaluator can follow it
   are allowed; they are not accelerators.
8. **Are diagnostic switches behaviour flags?** The survivors report extends
   `-X lucifer_jit_stats` and `LUCIFER_JIT_DEBUG`. They change no
   optimization decision. ENGINE's 2x2 "factorial builds" would be
   switchable optimizations. *Recommend:* diagnostics stay; ablations are
   branches, never switches in the tree.
9. **May a reference count read lower than CPython's while its holder is
   virtual?** Today a virtual tuple or instance keeps a counted reference
   to each field, so `sys.getrefcount` and `Py_REFCNT` of a child agree
   with CPython. PyPy's level needs those pairs gone. ROUND2 says
   no concession is needed, yet its own rule keeps every pair an observer
   could distinguish, which is all of them. Finalizer and weakref timing
   would not change: the last release stays where it is. CPython 3.14's
   borrowed stack references already changed what `getrefcount` returns.
   *Recommend:* allow it; exact counts are not part of the bar, release
   order is.
10. **Where may `MemoryError` surface when a virtual object is made
    late?** ROUND2 wants storage reserved where the source allocates, so a
    later exit or walker can never fail. A reservation is an allocation on
    the hot path and defeats the point. *Recommend:* no reservations. An
    exit that cannot allocate raises `MemoryError` at its instruction; a
    walker that cannot gets it in the walking thread. No abort (§5).

## 1. Baseline, verified at fba0b79

Instructions per iteration, user mode, pinned core, by process difference,
median of 5 alternated rounds (3 for the references). Spread is under 1.5%
on every Lucifer row quoted unless noted.

| loop | Lucifer | at 10db560 (packet) | CPython 3.14t | PyPy 3.11 | Lucifer ÷ CPython | ÷ PyPy |
|---|---|---|---|---|---|---|
| Mako cell (`l4`) | 10,612 | 14,377 | 4,421 | 1,296 | 2.40 | 8.2 |
| of which `html_escape` (5 `str.replace`) | 8,268 | 12,059 | 2,948 | 1,008 | 2.80 | 8.2 |
| pickle `save` | 43,326 | 56,019 | 12,185 | 682 | 3.56 | 63.5 |
| of which `pack("<B", n)` | 19,846 | 25,736 | 755 | (removed as dead) | 26.3 | |
| Genshi cell (`g3`) | 46,522 | 47,001 | 19,086 | 2,867 | 2.44 | 16.2 |
| of which `_eval_expr` and the yields (`g3 − g1`) | 39,789 | 37,966 | 13,066 | 1,515 | 3.05 | 26.3 |

Cycles per iteration (verified): Mako 5,127 / 1,965 / 515; pickle 28,568 /
6,150 / 168; Genshi 27,700 / 10,265 / 1,555.

Per-operation costs (verified; a new ladder, appendix B):

| operation | Lucifer | packet | CPython 3.14t | PyPy |
|---|---|---|---|---|
| positional call, callee not inlined | 215 | 227 | 488 | 98 |
| the same from an inlined frame | 251 | 296 | 489 | 0 (inlined) |
| keyword call | 435 | 481 | 664 | 99 |
| method call | 231 | | 504 | 141 |
| call binding `*args` | 777 | | 786 | 99 |
| call binding `*args, **kwargs` | 1,114 | | 971 | 103 |
| generator item, generator not resumed in line | 510–619 | ≈1,100 (Genshi) | 165–192 | 40 |
| `str.replace`, short, no match, inlined at the root | 1,633 | ≈2,400 (called) | 515 | 156 |
| `dict.get` (2 str keys) | 516 | | 375 | 112 |
| `list.append` | 234 | | 127 | 60 |
| `deque.append` (bound method) | 379 | 383 | 186 | 14 |
| `isinstance(x, (int, float))` | 430 | | 924 | 0 |
| `int.to_bytes(1, "little")` | 2,067 | | 622 | |
| `isinstance(x, str)`, instance attribute load | 4, 23 | | 242, 67 | 0 |

Whole programs (daily 2026-11-12, recorded at this commit, not re-run):
all 122 programs 2.086x CPython's time; pyperformance 2.475; cli 5.640;
AWFY 0.371; mako 49.4 ms (3.01x), genshi 398 ms (4.15x),
pickle_pure_python 2.10 ms (5.40x). Instructions per benchmark loop
(verified): mako 490.4M, of which the escaped-cell loop is 238.8M
(p = 0.49); genshi 2,476M; pickle 279.4M.

## 2. What the code and the numbers say

**Plain compiled calls are not the problem.** Every call shape but
`*args, **kwargs` beats CPython. Everything that enters the Python kernel
loses: a builtin container operation costs 1.4–2.0x CPython's, `str.replace`
3.2x, a generator item 3x, `struct.pack` 26x.

**The kernel wrapper is already inlined; three stacked blockers keep its
cost.** ENGINE's first increment assumes `str.replace(self, *args,
**kwargs)` is a separate unit entered through `lu_jit_bind`. On master the
Mako loop is one 74.7 KB unit with 45 inlines, `html_escape` and all five
wrappers among them, and each replace still costs 1,654 instructions for
about 170 of work. Per cell it allocates 5 tuples, 5 dicts and 5 lists
(verified with the allocation-site counts). Half the cell is allocation,
release and the critical sections of those objects. The blockers, in the
order the optimizer meets them:

1. **A local of any frame at a publishing instruction must be a real
   object** (`ssa_opt.c`, "a local where a walker reads it"). `H_prim`
   publishes even for a primitive that runs no Python, because it may
   release a value. So the wrapper's `args` tuple is built.
2. **Dead arms are built.** With rule 1 relaxed to the root and to pushed
   frames (a two-line experiment, verified, not gated), the tuple is still
   real: the `n == 3` arm's `old, new, count = args` loads a third field of
   a two-field tuple, and the builder neither folds a branch on a known
   size nor prunes the arm.
3. **`**kwargs` is a real dict.** Inline binding emits `H_new_dict`, and
   `ns_items(kwargs)` returns a real list to iterate.

Removing one leaves the cost where it is. Removing all three is worth 2.5x
on the cell: with the five wrappers replaced by hand with the primitive they
end in, the cell is **4,254** instructions (verified), under CPython's.

**Inlined frames are eager; the root frame is already lazy.** The root
publishes one pointer to its exit descriptor before a call (`jit_desc`,
`jit_spills`), and walkers read locals and the instruction through it
(`lu_jit_slot`, `lu_jit_instr`, `lu_jit_sync`). Inlined frames are pushed as
lent frames around every Python-running helper (`lu_jit_frames_push`, or
the same in line): about 15 stores per frame plus one per local, and every
local a real boxed object. The descriptor a call publishes already lists
the inlined frames. Only the walkers do not read them. Ticket luc-v0kt
describes the fix. The one-call experiment measured the consequence:
without the rule, chameleon +12.9%, pycodestyle +14.2%, markdown +7.7% in
instructions (packet).

**Library interpreters need their own lever, and it is smaller than
forecast.** A straight-line packer for `"<B"` written by hand runs 2,174
instructions (verified) against 19,846, because `int.to_bytes` is itself
2,067. So the lever is not a packer per format but an evaluator that follows
the constant format through `_struct` as written, down to a byte store
(`jit-architecture.md` §2).

**Genshi has no single cause.** Its profile (verified, by symbol) is a long
tail: the interpreter 14% (eval'd code is never compiled), type lookups 7%,
frame and call protocol about 10%, generator resumes, GC. No one mechanism
is worth more than about 15% of the cell.

## 3. Architecture of record

Superseded by `jit-architecture.md`: the per-layer audit (its §2), what is
fundamental and what is inherited (§3), the end state (§4) and what is
deleted (§5). Two things carry over unchanged: one compiler, one IR and
the interpreter as the exit target; and no task-owned activation stack
(decision 3).

## 4. Targets

The milestones M0–M12 are superseded by the stages of
`jit-architecture.md` §7, which lists what each stage subsumes. Per-stage
predictions are in its §9.

Instructions per iteration:

| loop | now (verified) | target (estimated) | PyPy (verified) | CPython 3.14t (verified) |
|---|---|---|---|---|
| Mako cell | 10,612 | 1,200–1,700 | 1,296 | 4,421 |
| pickle `save` | 43,326 | 1,000–1,400 | 682 | 12,185 |
| Genshi cell | 46,522 | 4,000–5,000 | 2,867 | 19,086 |

The targets are ROUND2's, which adopts the side-by-side study's estimate
of what a cp314t runtime with PyPy's optimizer still executes (Mako
widened by 100–200). Mako's is supported by measurement: it is one unit
already, its real work is 880 instructions (packet) and the emulation
reaches 4,254 (verified). Nothing measured confirms or contradicts the
other two; they are the targets regardless, and the plan is judged
against them.

The loops are not the programs. With the Mako cell at its target the mako
benchmark is still 279–290M instructions against CPython's 159.4M (derived
from verified counts): the cell is 49% of it.

Compatibility: the targets exceed PyPy by −200 to +200 (Mako), 300–700
(pickle) and 1,100–2,100 (Genshi) instructions (packet estimates), under
5% of each gap. The only concessions used are decisions 9 and 10.

## 5. Correctness plan

The gates are the existing ones, over the floor recorded with the tier off
(`jit-architecture.md` §6); nothing lands without all five green:
`make check` (kernel programs plain, `jit` and `jit-deopt` lanes: 711 each),
`make abi-audit`, `make check-jit` (jitcheck; difftest `always` and
`deopt:7`: 193 each), `make check-harness`, `make check-compiler`. Tests are
binary-level against the CPython oracle and are written first.

| mechanism | new binary-level tests | lane |
|---|---|---|
| Virtual frames | From a callee of an inlined frame: `sys._getframe(n)` identity and `f_back` chain, `f_lineno`, `f_locals` read and write-through with the next use, a traceback kept past the frame, `settrace` and `sys.monitoring` turned on inside the callee with the full event log, the same from a C extension callback (`PyEval_GetFrame`, `PyFrame_GetBack`), `sys._current_frames` from another thread, `faulthandler` dump. Each compared with the oracle. | check-kernel `jit`, `jit-deopt`; check-capi-ext |
| Virtual objects in locals | `id()` and `is` of a tuple or instance seen by a walker and again after the exit; a walker mutating a virtual instance | check-kernel `jit-deopt` |
| Every exit resumes once | `deopt:N` already fails every Nth guard; add a seed sweep (seed 0 misses sites, packet) and a count-of-effects test per boundary kind: before an effect, after it, raising | difftest `deopt` |
| Binding plans | The CPython `test_extcall`, `test_keywordonlyarg`, `test_positional_only_arg` shapes through compiled callers: duplicate keywords, mutated `__defaults__` and `__kwdefaults__` between calls, kwargs retained, mutated and passed to C | check-kernel `jit` |
| Effect summaries | Each summary is checked by a stress mode that runs a finalizer, a collection and a class mutation at every primitive call that claims it cannot be observed; a wrong claim fails differential output | new `lucifer_stress` kind in check-kernel-jit |
| Entries by shape | Rebind the callee, change defaults, install a trace function, a PEP 523 hook and monitoring between and during calls; recursion to the limit; the 512 KiB stack lane | check-kernel `jit`; deep_recursion |
| Namespace entries | `exec` and `eval` with dict subclasses, a `__prepare__` mapping, `__builtins__` replaced, names deleted and rebound between runs | check-kernel `jit` |
| Generators | `send` before start, `throw`, `close` with `finally`, reentrant resume, `gi_frame` and `gi_running` from the consumer and from a callee, `StopIteration` conversion | check-kernel `jit`, `jit-deopt` |
| Free threading | Handoff and `gc:N` lanes run with `-X lucifer_jit=always` and `deopt:N` together (today they run by hand without the JIT flags); a writer/reader test that requires the post-acknowledgement lookup to see the new binding | check-kernel, new lane |
| No allocation-failure aborts | Allocation-fault injection at exits and frame materialization; no fatal path in the materializer | new lane |
| Virtual object kinds | Per kind: identity and contents of an object made on a cold edge, seen from Python and from a C extension; an exception before and after the store that publishes it, with a count of effects; a walker forcing it mid-call and the caller's alias afterwards; `gc.get_objects` and `gc.get_referrers` from a callee | check-kernel `jit`, `jit-deopt`; check-capi-ext |
| Ownership and locks | Two threads publishing, replacing and clearing a container while compiled code fills or reads it; a finalizer and a weakref callback on a value whose pair was removed, order compared with the oracle; the summary stress kind run at every moved or borrowed reference | handoff and `gc:N` lanes with the JIT |
| Byte parts, dict probes, namespaces | Output bytes and exception text against the oracle for non-ASCII, an exported buffer, a seek between writes, a writer replaced mid-`save`; a dispatch key rebound, an `id` collision with a non-int key; a locals mapping that mutates itself, an audit hook that raises, `__builtins__` present after `eval` | check-kernel `jit`, `jit-deopt` |
| Fused and in-line resumes | The generator tests through a fused pair, plus `throw` and `close` on each side of a yield and two threads resuming one generator | check-kernel `jit`, `jit-deopt` |
| Performance | The ladders, checked in with bounds; the 39 jitcheck growth lanes (packet), rewritten for the new design | check-jit; daily |

Reviews are assigned per stage in `jit-architecture.md` §7.

## 6. Risks

The design's own risks are in `jit-architecture.md` §9. These remain:

| risk | measurement that settles it |
|---|---|
| Binding plans help only Mako | Programs outside the band at the slice. Also the count of kernel functions taking `*args` (177 definitions in `lib/kernel`) that fold fully. |
| Lazy frames hide roots from the GC's mark-alive pass, so it does more work | GC instructions per Genshi cell, before and after. Frame references stay counted, so only speed is at stake. |
| A park under compiled callers deopts the whole native chain | Not measurable until a scheduler exists. Then: parks per 1,000 calls and frames rebuilt per park on the web tiers. This is what would reopen decision 3. |
| The targets have no margin: they assume an optimizer as good as PyPy's on every line at once | Each stage's gate, against its measured parent. A missed gate is priced by the survivors report before the next stage starts. The target does not move. |
| Sinking grows code: a make on every cold edge, and exits that name more objects | Code and descriptor bytes per program in the daily; makes share one out-of-line stub per kind. |
| A callee recompile voids its summary and kills every caller that relied on it | Units killed per callee recompile over the corpus. If callers die in chains, a caller relies on the summary's content hash, not the unit. |
| The mechanisms fit the three loops and little else | Every stage names programs outside the band. A mechanism that moves only its loop does not land. |
| Kernel Python is slower than the C it replaces even when compiled well (`int.to_bytes` 3.3x, `dict.get` 1.4x) | The per-operation ladder once S2 lands. Any operation still over 1.5x CPython gets a primitive under it, which the bet allows; a module moved to C would not be. |

## Appendix A. Corrections to ENGINE and ADAPTIVE

Checked against `fba0b79`. C: confirmed. W: wrong, with what is true.
D: done since `10db560`.

| claim | status |
|---|---|
| The baselines: Mako 14,377, pickle 56,019 (pack 25,736), Genshi 47,001; calls 227 / 296 / 481 | D. Now 10,612, 43,326 (19,846), 46,522; 215 / 251 / 435. Of the merges named as newer than `10db560`, luc-kc1, luc-621k and luc-setmerge were already in it; luc-kc3, luc-regex, luc-region1 and luc-aio are newer. |
| Increment 1: `str.replace`'s wrapper is a separate unit; specializing its binding makes five replaces cost 500–1,000 each | W. It is inlined on master. Three stacked blockers keep the cost (§2); binding is the third. The end state is better than forecast: 382 per replace. |
| Increment 2: a returning-call path "backed initially by the existing materialization paths" recovers 30–60% of Genshi's call protocol | W as sequencing. That is the one-call experiment, which lost. Its own forecast is 3–6% of the cell. |
| Increment 3: a generated packer costs 1,000–4,000 | C at the top of the range only: 2,174, because `int.to_bytes` is 2,067. |
| "Extend `lu_fs`", intern states | C, with a correction: `lu_fs` is compile-time. The run-time form is `lu_deopt`, one flat private copy per exit. The root frame is already lazy through it; ticket luc-v0kt already proposes the same for inlined frames. |
| Virtual `*args` must be added | W. Inline binding makes the rest tuple with `H_build_tuple`, which scalar replacement already removes when nothing forces it. `**kwargs` is a real `PyDict_New`. Out of line, `lu_jit_bind` allocates the tuple only for extra positionals and the dict always. |
| `call_direct` re-checks type, executors, arity and entry; the direct entry re-runs the interpreter's checks | C: dead unit, stack floor, poll word against the instrumentation version, `_co_monitoring`, tracing, the eval-frame hook, recursion. |
| A nested `lu_eval_continue` pins the task | C (`lu_jit_call_finish`, `call_function`). But migration is the two-carrier handoff stress policy only, so the task stack fixes nothing measurable (decision 3). |
| `NOPY` primitives may allocate and raise | C. 85 of about 515 primitives. Missing from the claim: `H_prim` still publishes (`IF_SEEN`), which is blocker 1. |
| Twins: 13, exact arity, JIT only | C. |
| Eval'd code never compiles | C: `compile_at` and `not_inlinable` require `CO_OPTIMIZED`; `lu_jit_run` refuses a frame with a locals mapping. |
| A generator resumed in line cannot call Python | C (fatal in the push; the builder refuses first). New: a generator loop entered by OSR is never resumed in line, since its code is unknown at entry: 619 instructions per item. |
| Invalidation raises a flag on every task | C: `signal_all` takes the interpreter's task-list mutex and sets `LU_POLL_DEOPT_SELF` on each; every compiled poll leaves on it. |
| Fatal paths in the deoptimizer | C: three `Py_FatalError` sites for no room, and unchecked allocations in `take`. |
| Coverage spends 21% finding the previous instruction; the GC walks the whole heap; asyncio is 3–4x | D. The bitmap was already in `10db560` (coverage is 4.54x in the daily); the mark-alive pass landed with luc-aio; async_generators went from 13.4x to 3.7x. |
| 694 kernel programs, 191 difftest programs | D. 711 and 193. |
| A private call costs 15–45 instructions as a jump with a saved continuation | Unverified, and the target ignores that a return by indirect jump loses the return predictor. Native calls keep it. |
| CPython's positional call is about 593 instructions | W on this ladder: 488. PyPy's residual call, which ENGINE lists as unmeasured, is 98. |
| ADAPTIVE M1: prove `str(int)` contains no escaped character and delete the five searches | Benchmark-shaped: the Mako benchmark escapes `range(150)`. Not adopted (decision 5). |
| ENGINE and ADAPTIVE disagree on when the task stack lands (after increment 2; in M2 "in slices") and neither lands lazy frames before widening inlining | Resolved: frames are virtual from the first slice; the task stack is not built. |

## Appendix B. Method

- Counts are `perf stat -e instructions:u,cycles:u` on one pinned core:
  each case runs at two repetition counts and the per-iteration cost is the
  difference divided by the extra iterations, so startup and warm-up
  cancel. Runtimes alternate within a round; the median of 5 rounds is
  reported (3 for CPython and PyPy).
- The three loops are the ceiling study's ladders (its §6), unchanged.
- The call ladder is new. A callee that recurses is never inlined, so
  `rec(K)` is one call from an inlined frame and K − 1 direct calls; a
  per-call cost is the slope between K = 2 and K = 12 and includes the
  callee's trivial body. Builtin operations are a loop of the operation
  minus the empty loop.
- PyPy removes an unused `pack` entirely, and inlines recursion to its own
  depth, so its isolated rows are lower bounds on its real cost.
- CPython is 3.14.10 free-threaded; PyPy is 7.3.23 for Python 3.11.
- The emulations: Mako with each `s.replace(a, b)` written as the primitive
  call the wrapper ends in; `pack("<B", n)` as a type test, a range test
  and `n.to_bytes(1, "little")`.
- Whole-program wall times are the daily's and were not re-run. Cycle
  counts by process difference on whole benchmarks spread 25–130% and are
  not used.
- Stage S0 commits the ladders; until then they live with this branch's working
  files.

## Appendix C. Round 2, checked

ROUND2's claims about the code, checked at `2dc44d5`, and its arithmetic
on the baselines. C: confirmed. W: wrong as a statement about the tree.
S: stale (already there). U: under-specified. B: not in the tree, to
build (`jit-architecture.md` section in brackets).

| claim | status |
|---|---|
| Ladder differences, ratios to PyPy and CPython, gap shares, the pickle mix (42 new strings, 2 hits, 13 `BININT1`, 4 `BININT`, 1 `LONG1`; 104 writes per 62 saves), the weighted pickle budget (895–1,218 plus 110–180), the Genshi sums, the mako whole-benchmark figure | C, all re-derived. |
| "Compatible engineering targets" of 1,000–1,400 (pickle) and 4,000–5,000 (Genshi); compatibility costs at most 5% of each gap | Adopted as the targets (§4). They are the side-by-side study's estimates (Mako plus 100–200), not an independent derivation; nothing measured confirms or contradicts them. |
| pickle's new-string path budget, 1,020–1,380 | U. Its itemized lines sum to 305–615; the other 400–1,075 is driver, frames and guards, unitemized, where 406 guards and 24 calls run today. The stage that removes those must show it. |
| Mako: five searches in 800–950 | U. The searches' own work is 880 today (packet), leaving at most 14 instructions per call for the call, the no-match test and the reference. Tested at the first slice. |
| Genshi: `g2 − g1` ≤ 28,000 as the first gate after namespace entries | U. The last step, 8,000 → 2,450, is assigned to "remaining expression fast paths" with no mechanism named. The architecture's answer is evaluation through `eval` with virtual namespaces [§4.2, S7]. |
| The trace-region experiment runs "after M1/M4/M6" (its §6) and "after profiled M12 landings" (its §7) | Inconsistent. Here: tail duplication is built in; a recorder is tried only if joins cost over 10% of a band loop [§9]. |
| The projected-tuple increment needs M4 and M6 | B. Virtual aggregates as entry fields, with a second return word for a callee that kept one [§4.4, S8]. |
| "Extend `objects`, `escape`, `rewrite` and their phi handling" in `ssa_opt.c` | B, by replacement. `objects` makes an object virtual only if every use qualifies; `escape` covers boxes and an unfilled dict. Per-path virtual objects live in the evaluator's state, and these passes go [§4.1, §4.2]. |
| "Finite field paths already used by the partial evaluator" | B. The tree has neither. The partial evaluator is the new builder; a field path is a field of a virtual object in its state [§4.1, §4.2]. |
| Publish summaries through "the compile ticket and dependency publication in `deps.c`" | C for the protocol (`lu_jit_deps_begin`, `lu_jit_deps_publish`). B for the dependency on a callee's unit and summary: the kinds today are a type's version, a dict's keys, one value of a dict, a function's code and defaults, and a call site's callee set [§4.4]. |
| An "exact dict value dependency" is to be added for `dispatch` | S. `LU_DEP_VALUE` exists and `dict_const` uses it (keys-version guard plus an identity guard on the value word). The limit is that `dict_const` takes constant names in string-keyed tables; `dispatch` is keyed by types. |
| A returning call with normal, raised and deopt or park continuations | B. The raise edge exists (`catcher`, `catch_here`); deopt and park are exits. A callee that leaves compiled code sets its caller's forced word, which the caller tests after the call [§4.3, §4.4]. |
| An internal entry "keyed by the guarded representation" | B. Today a code object has one live unit per entry instruction and one direct entry taking borrowed `PyObject **`. Entries keyed by (entry instruction, shape) replace it [§4.4]. |
| Generated packers in `lib/kernel/_struct.py`; "the codec/IO kernel" | W. `lib/modules/_struct.py`, `_io.py`, `_codecs.py`. |
| `eval` "retires after 50 entries/50 exits" | C. A unit without loops is retired after 50 leaves on at least half its entries; each retirement doubles its wait, nothing is permanent. |
| M8 touches `compile_at`, `not_inlinable` and `lu_jit_run` | C. The first two test `CO_OPTIMIZED`; `lu_jit_run` refuses a frame with a locals mapping. |
| `lu_fs` stays compile-time; frames extend `lu_deopt` | C. |
| The fatal materialization paths | C. `deopt.c`: out of memory rebuilding a virtual object, three for no room, and four that assert invariants (a published value in a register, a call from a generator resumed in line, a virtual local in a lent frame, kept frames that do not match). |
| Frame references are counted; the deferred bit serves caches and code, with the lent-code exception | C. Deferred: code objects, top-level functions, descriptors. |
| Lock elision exists for instances, not containers | C. |
| References: "keep the RC operations when a legal observer of that child could distinguish their removal", with "no concession needed" | W, inconsistent with its budgets. Any callee can read a count. Today's virtual objects keep counted references to their fields; removing them is decision 9. |
| "Deopt cannot invent a new allocation exception": reserve storage at the source allocation | Not adopted (decision 10). |
| Heap enumeration: "disable that virtualization class until enumeration is supported" | Not adopted: a self-disabling optimization is a behaviour switch. Enumeration forces frames. |
| A segment return convention for strings and bytes carried across out-of-line calls | B, as one kind among the others: parts are fields of a virtual string or bytes, and cross a kept call as any aggregate's fields do [§4.1, §4.4]. |
| The Smalltalk "escaped but unread" idea | U. Not in the donor tours. |
| The first slice is M0 → M1 → the projected tuple, pulling M4, M5 and M6 forward | Replaced. The slice is the evaluator, virtual frames and the materializer on the Mako cell, where the claim can be measured against 880 instructions of real work; aggregates across kept calls follow as S8 [§8]. |
