# JIT redesign: architecture of record and roadmap

2026-11-12. Written against master at `fba0b79`. Inputs: the ceiling study
(`jit-ceiling-2026-11-11.md`), the corpus-gap study
(`corpus-gap-2026-11-11.md`), the one-call-rule experiment (branch
`luc-region`, `31721c4`), and two external design documents written from a
description of master at `10db560` without access to the code: an execution
engine design (cited as ENGINE) and an adaptive optimization roadmap (cited
as ADAPTIVE). This document checks both against the code, re-measures the
numbers they rest on, and replaces their sequencing.

Every number is marked **verified** (measured for this document on
`fba0b79`) or **packet** (taken from the studies or the external documents,
not re-measured). Method is in appendix B.

## 0. Owner decisions

Open. Each has a recommendation; the roadmap below assumes the
recommendations.

1. **What is the bar?** ENGINE and ADAPTIVE set "10x CPython and within 3x
   of Go". The brief said PyPy's level on real programs and multiples on
   compute. *Recommend:* gate milestones on instructions per iteration of
   named loops against CPython and PyPy (§1), keep the 122-program geomean
   as the headline, and treat 10x on compute as a later bar. No milestone
   here is priced to reach it.
2. **Lazy frames before call-shape binding?** ENGINE lands binding first and
   lazy frame states after its second increment. *Recommend:* lazy frames
   first (M1). §2 shows binding has no effect on Mako until two other
   blockers go, and the one-call experiment showed nothing that widens
   inlining pays while inlined frames are pushed eagerly.
3. **Build the task-owned activation stack?** ENGINE makes compiled calls
   jumps with continuation records on a task stack, so a task can migrate
   under compiled callers. *Recommend:* no, not now. Migration exists only
   as the two-carrier handoff stress policy, so a pin costs nothing
   measurable today, and DESIGN 5.2 (no stack switching, a park returns to
   the carrier loop) already holds if a park stays an exit. Revisit when a
   real scheduler exists and parks per call are measured on the web tiers.
4. **Private entries: how early?** They are M5 here, after binding and
   effects. A compiled positional call is already 215 instructions against
   CPython's 488 (verified); the call protocol is 7–10% of the three band
   loops (packet). *Recommend:* keep M5. It is a 10–20% lever on call-heavy
   code, not the multi-x part.
5. **ADAPTIVE's content proofs and M5–M7** (the proof that `str(int)` has
   no character `html_escape` replaces; adaptive data layout; indexes;
   incremental recomputation). The only evidence offered is the Mako
   benchmark's own input. *Recommend:* off the roadmap. Reopen one when a
   second, unrelated program shows the pattern.
6. **A persistent code cache and a compiled kernel image for one-shot
   runs.** Compile plus import is 25–60% of a first run (packet), and the
   cli suite is the worst row (5.64x, daily). *Recommend:* M11, after the
   unit shape settles, but apply the constraint now: new metadata (frame
   descriptors, binding plans, struct plans) holds no raw pointers that a
   constant table could hold.
7. **Kernel code written for the compiler.** M7 generates per-format
   packers in `_struct` and needs one new primitive that stores bytes into a
   fresh `bytes`. `int.to_bytes` alone costs 2,067 instructions (verified).
   *Recommend:* allow generated kernel Python and small primitives of this
   kind; they are not accelerators.
8. **Are diagnostic switches behaviour flags?** M0 extends
   `-X lucifer_jit_stats` and `LUCIFER_JIT_DEBUG`. They change no
   optimization decision. ENGINE's 2x2 "factorial builds" would be
   switchable optimizations. *Recommend:* diagnostics stay; ablations are
   branches, never switches in the tree.

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
2,067. Generated plans need a byte-store primitive under them to reach
CPython's 755.

**Genshi has no single cause.** Its profile (verified, by symbol) is a long
tail: the interpreter 14% (eval'd code is never compiled), type lookups 7%,
frame and call protocol about 10%, generator resumes, GC. No one mechanism
is worth more than about 15% of the cell.

## 3. Architecture of record

ENGINE's terms, corrected against the code. One compiler, one IR, one
interpreter as the exit target, as now.

**Region.** The unit of compilation stays "one entry at a bytecode
boundary" (call entry, loop header, generator resume) and grows by
inlining over block SSA with joins. What changes is selection: sites carry
execution counts, budgets count IR after folding rather than bytecode
units, and a unit that stays hot is recompiled with a larger budget. The
four fixed limits (400 units, depth 4, 3,000 inlines, the one-call rule)
go. No trace recorder and no second backend.

**Activation and frame state.** A logical Python frame in compiled code is
a frame state: code, instruction, locals, stack, ownership, how it returns.
At compile time this is `struct lu_fs`, a node with a `caller` link. At run
time it is `struct lu_deopt`, today a flat copy of every frame per exit;
it becomes the same linked shape, one node per frame per safepoint, the
caller chain shared by every safepoint inside an inline. Three consumers
read one description, as today: exits, calls, walkers.

**Lazy frames.** No frame of a compiled activation is made real until
something observes it. Before a call that may run Python, compiled code
stores the descriptor pointer (two stores, as the root does now). Every
walker goes through one accessor that, on meeting a frame with a
descriptor, makes the inlined frames it names: heap frames linked between
the root and its callee, values taken from the spill words. A frame so
observed is canonical from then on: virtual objects are built once and
remembered per activation, and compiled code leaves at its guard after the
call (`GUARD_FRAMES`, already there), handing those frames to the
interpreter as it hands kept frames now. Deleted: lent frames,
`lu_jit_frames_push` and `_pop`, the in-line push, the kept-frame matching,
both "locals stay real" rules, and the fatal paths in the push. A park is
an exit, as now.

**Returning call.** A call the region keeps is an IR instruction with a
callee, arguments, an effect summary and normal and exceptional
successors, not a helper with flags. Facts and virtual objects survive it
according to the summary. It is backed by the existing direct entry until
private entries land.

**Private entry.** A caller that has guarded the callee enters a unit's
body past the checks it proved. The checks that are properties of the
world (tools on, a hook installed, the unit dead) move from every call to
invalidation: the caller depends on the callee unit (`LU_DEP_CALLEES`
exists). The callee's frame is lazy by the same mechanism as an inlined
one. Calls stay native calls on the C stack, bounded by the existing stack
floor.

**Binding plan.** One description of how a call shape binds to a signature
(positional, keyword names, defaults, rest, keyword rest), computed once
per (code, shape), read by the C binder and the builder. For an inlined
call the rest tuple is virtual (it already is) and the keyword dict is a
virtual mapping with a known key set. For a call that stays out of line, a
shape-specialized entry binds in compiled code. The 13 hand-written twins
go, one by one, as their wrappers fold.

**Folding at build.** The builder folds what its facts decide: a branch on
a constant or a known size, an unpack or index of a known tuple, iteration
over a tuple of known length (unrolled) or an empty mapping. Arms it folds
away are not built.

**Effect summary.** Each primitive and helper states what it may do: read
or write which heap facts, allocate, raise, run Python, release a value
that may finalize, park. This replaces `LU_METH_NOPY`, `LU_METH_FRAME`,
`IF_PY` and `IF_SEEN`. An instruction publishes only if it may be observed
and kills only the facts it may write. A compiled callee carries a
versioned summary of the same kind, so a returning call need not kill
everything.

**Plans in kernel Python.** Where a stdlib module interprets a stable
input (a struct format, later a codec choice or a BytesIO write path), the
module generates straight-line Python per input from its one parser, as
`_sre` does per pattern, and keeps the general executor for cold inputs.

**Namespace entry.** Code without `CO_OPTIMIZED` (eval, exec, class and
module bodies) compiles when its namespaces are exact dicts; name lookup
is locals, globals, builtins with dict-key dependencies. Any other mapping
runs in the interpreter.

**Generators.** A generator frame is an activation like any other: resumed
in line when its code is known (guarded, including at a loop entered by
OSR), allowed to call Python once frames are lazy, and otherwise resumed
through a private resume entry rather than `lu_jit_resume`.

**Invalidation.** A unit that relies on a fact checks its own `dead` word
at its polls. A writer kills the dependent units and signals nobody else.

Not adopted: the task-owned activation stack (decision 3), content
repertoire proofs, storage strategies, indexes and incremental
recomputation (decision 5), a baseline compiler.

## 4. Milestones

Dependency order. Each is one branch, lands on its own behind the gates in
§5, and states a prediction that can fail. "h" is agent-hours (builder plus
review), from this repository's measured rate. Wall time uses
T′ = T·(1 − p·(1 − r)), p the loop's verified share of the program's
instructions and r the loop's predicted instruction ratio.

| | milestone | needs | h | predicted effect (falsifier) |
|---|---|---|---|---|
| M0 | Survivors report and committed ladders | | 4 | No instruction change (±0.5%) with stats off. The report names, for the three loops, every surviving allocation, call, publish and refcount pair with its count and the rule that kept it. |
| M1 | Lazy inlined frames; then drop the one-call, depth and loop-after rules | M0 | 25 | Call from an inlined frame 251 → ≤ 220. With the rules gone, every program the experiment made slower is now no slower than master (chameleon, pycodestyle, markdown, mako, pickle: ≤ 0%), and Json, Havlak, DeltaBlue keep ≥ 4% fewer instructions. pickle `save` and Genshi cell ≤ −5%. black's code bytes ≤ 1.3x. |
| M2 | Folding at build; post-fold budgets | M0 | 15 | `isinstance(x, (int, float))` 430 → ≤ 60. Mako allocates 0 tuples per cell (from 5) once M1 is in; cell ≤ 9,900. No program compiles more than 5% slower. |
| M3 | Binding plans: virtual keyword rest, shape-specialized entries | M1, M2 | 20 | Mako cell ≤ 4,700 (bound: 4,254 by emulation); 0 tuples, dicts and lists per cell at `html_escape`. `str.replace` 1,633 → ≤ 450. Calls: keyword 435 → ≤ 260, `*args` 777 → ≤ 300, `*args, **kwargs` 1,114 → ≤ 320. mako 49.4 → 33–37 ms. Generality: chameleon, typing_runtime_protocols and pickle each ≥ 5% fewer instructions, or the mechanism is Mako-shaped. |
| M4 | Effect summaries for primitives and helpers | M0 | 20 | `deque.append` 379 → ≤ 150, `list.append` 234 → ≤ 90, `dict.get` 516 → ≤ 250. Liveness checks per Bounce `bounce()` 11 → ≤ 3. AWFY geomean 0.371 → ≤ 0.33. |
| M5 | Private entries; lazy callee frames | M1, M4 | 30 | Positional call 215 → ≤ 80, method 231 → ≤ 90. Richards 1,898 → ≤ 1,650 ms. pickle `save` and Genshi cell each a further ≥ 5%. |
| M6 | Returning-call instruction; site counts; hot recompile; callee summaries | M1, M4 | 35 | pickle `save` and Genshi cell each a further ≥ 10%. No AWFY row more than 2% slower. black one-shot time ≤ +3%. |
| M7 | Generated struct plans and a byte-store primitive; then `BytesIO.write` and `str.encode` paths | none | 10 | `pack("<B", n)` 19,846 → ≤ 2,300 at the first landing, ≤ 600 with the primitive. `save` 43,326 → ≤ 26,000. pickle_pure_python 2.10 → 1.4–1.6 ms; unpickle_pure_python ≥ 20% faster. |
| M8 | Namespace entries | M0 | 18 | Genshi cell ≤ 41,000; genshi 398 → ≤ 380 ms. The interpreter's share of the cell falls from 14% to ≤ 4%. |
| M9 | Generators: guarded in-line resume at OSR, calls from in-line resumes, private resume entry | M1, M5 | 25 | Generator item 510–619 → ≤ 200 out of line, ≤ 60 in line. Genshi `g1` 6,734 → ≤ 4,500. pyperformance generators 1.98x → ≤ 1.3x; async_tree 3.57x → ≤ 3.0x. |
| M10 | Targeted invalidation | M0 | 8 | With 8 threads in compiled loops and one thread redefining an unrelated class, exits in the 8 threads: 0 (now one each per redefinition). |
| M11 | Persistent unit cache and compiled kernel image | M6 | 40 | cli geomean 5.64 → ≤ 4.0; isort, markdown, black first runs ≥ 30% faster. |
| M12 | Ownership and escape across returning calls: refcount pairs, locks on unpublished objects, allocation removal through summaries | M4, M6 | 50 | Bounce 627 instructions per operation (packet) → ≤ 250; AWFY geomean ≤ 0.22. Unpriced beyond this first slice. |

About 300 agent-hours. M0, M2, M4, M7, M8 and M10 have no dependency on M1
and run beside it; at most two JIT-core branches (`ssa_build.c`,
`ssa_opt.c`) at once, to keep merges small.

**M0.** The pieces exist: `-X lucifer_jit_stats` counts executed guards,
references, calls and allocations by site; `LUCIFER_JIT_DEBUG` prints why
an object is not virtual and why a callee is not inlined. M0 joins them
into one ranked table per unit (rule, witness instruction, executed count)
and commits the loop and call ladders used here to `tools/bench`, with
their CPython and PyPy columns. It is the acceptance check for M1–M3,
whose first two change Mako little by themselves.

**M1.** Stage 1: the linked run-time descriptor and the walker accessor
(about 30 uses of `->previous` outside `runtime/jit`, in seven files), inlined
frames observed through it, lent frames deleted. Stage 2: virtual objects
as locals, built once per activation. Stage 3: delete the one-call, depth
and loop-after rules (rebase `31721c4`). `faulthandler` walks frames in a
signal handler and cannot allocate: it reads code and line from the
descriptor without making frames.

**M5.** Stage A, the proved entry, needs only dependencies. Stage B, the
lazy callee frame, reuses M1's accessor for root frames.

**End state after M1–M9**, if every prediction holds: Mako cell about
3,700 (0.85x CPython, 2.9x PyPy), pickle `save` about 15,000 (1.2x, 22x),
Genshi cell about 30,000 (1.6x, 10x). M12 is where the rest of the gap to
PyPy is, and it is the least certain item here.

## 5. Correctness plan

The gates are the existing ones; nothing lands without all five green:
`make check` (kernel programs plain, `jit` and `jit-deopt` lanes: 711 each),
`make abi-audit`, `make check-jit` (jitcheck; difftest `always` and
`deopt:7`: 193 each), `make check-harness`, `make check-compiler`. Tests are
binary-level against the CPython oracle and are written first.

| mechanism | new binary-level tests | lane |
|---|---|---|
| Lazy frames (M1, M5) | From a callee of an inlined frame: `sys._getframe(n)` identity and `f_back` chain, `f_lineno`, `f_locals` read and write-through with the next use, a traceback kept past the frame, `settrace` and `sys.monitoring` turned on inside the callee with the full event log, the same from a C extension callback (`PyEval_GetFrame`, `PyFrame_GetBack`), `sys._current_frames` from another thread, `faulthandler` dump. Each compared with the oracle. | check-kernel `jit`, `jit-deopt`; check-capi-ext |
| Virtual objects in locals | `id()` and `is` of a tuple or instance seen by a walker and again after the exit; a walker mutating a virtual instance | check-kernel `jit-deopt` |
| Every exit resumes once | `deopt:N` already fails every Nth guard; add a seed sweep (seed 0 misses sites, packet) and a count-of-effects test per boundary kind: before an effect, after it, raising | difftest `deopt` |
| Binding plans (M3) | The CPython `test_extcall`, `test_keywordonlyarg`, `test_positional_only_arg` shapes through compiled callers: duplicate keywords, mutated `__defaults__` and `__kwdefaults__` between calls, kwargs retained, mutated and passed to C | check-kernel `jit` |
| Effect summaries (M4) | Each summary is checked by a stress mode that runs a finalizer, a collection and a class mutation at every primitive call that claims it cannot be observed; a wrong claim fails differential output | new `lucifer_stress` kind in check-kernel-jit |
| Private entries (M5) | Rebind the callee, change defaults, install a trace function, a PEP 523 hook and monitoring between and during calls; recursion to the limit; the 512 KiB stack lane | check-kernel `jit`; deep_recursion |
| Namespace entries (M8) | `exec` and `eval` with dict subclasses, a `__prepare__` mapping, `__builtins__` replaced, names deleted and rebound between runs | check-kernel `jit` |
| Generators (M9) | `send` before start, `throw`, `close` with `finally`, reentrant resume, `gi_frame` and `gi_running` from the consumer and from a callee, `StopIteration` conversion | check-kernel `jit`, `jit-deopt` |
| Free threading | Handoff and `gc:N` lanes run with `-X lucifer_jit=always` and `deopt:N` together (today they run by hand without the JIT flags); a writer/reader test that requires the post-acknowledgement lookup to see the new binding (M10) | check-kernel, new lane |
| No allocation-failure aborts | Allocation-fault injection at exits and frame materialization; the three fatal paths in `deopt.c` become recoverable or unreachable (M1 deletes two) | new lane |
| Performance | The M0 ladders, checked in with bounds; the 39 jitcheck growth lanes (packet), rewritten where they assert the units M1 and M6 change | check-jit; daily |

Reviews: M1, M5, M9 and M10 touch frames, GC roots and cross-thread state
and get the full review with tests; the rest get the routine one.

## 6. Risks

| risk | measurement that settles it |
|---|---|
| Observed frames deopt too often: logging's `findCaller`, tracebacks that outlive frames, and `inspect` users make frame objects on hot paths | `EXIT_FRAME` exits per 1,000 calls over the corpus before and after M1. If any program exceeds 1, let an observation that wrote nothing and took no virtual object be dropped without an exit. |
| Deep inlining grows code and compile time again (black went from 14.6 to 45.8 MB unbounded, packet) | Code bytes, descriptor bytes and compile ms per program in the daily, from M1 on. The linked descriptor should make descriptor bytes grow with frames, not frames × exits. |
| M1 and M2 show nothing on programs until M3 | M0's report: the named rules disappear from the three loops' survivor tables. That, not wall time, gates M1 and M2. |
| Binding plans help only Mako | The M3 generality row. Also the count of kernel functions taking `*args` (177 definitions in `lib/kernel`) that fold fully. |
| Effect summaries are wrong | The M4 stress kind. A wrong summary is a miscompile, so the default for an unlisted primitive is "may do anything". |
| Lazy frames hide roots from the GC's mark-alive pass, so it does more work | GC instructions per Genshi cell before and after M1. Frame references stay counted, so only speed is at stake. |
| Generated packers and plans cost more to make than they save in one-shot runs | First-run time of pickle, unpickle and the cli suite with M7; generate on reuse count, not on first use. |
| A park under compiled callers deopts the whole native chain | Not measurable until a scheduler exists. Then: parks per 1,000 calls and frames rebuilt per park on the web tiers. This is what would reopen decision 3. |
| The band reaches CPython's level and stops there | The end-state row in §4. If M3 and M7 hit their bounds and the loops are still over 2x PyPy, the remainder is allocation, refcounts and locks: M12 moves up. |
| Kernel Python is slower than the C it replaces even when compiled well (`int.to_bytes` 3.3x, `dict.get` 1.4x) | The per-operation ladder after M4. Any operation still over 1.5x CPython gets a primitive under it, which the bet allows; a module moved to C would not be. |

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
| ENGINE and ADAPTIVE disagree on when the task stack lands (after increment 2; in M2 "in slices") and neither lands lazy frames before widening inlining | Resolved here: lazy frames are M1, the task stack is not built. |

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
- M0 commits the ladders; until then they live with this branch's working
  files.
