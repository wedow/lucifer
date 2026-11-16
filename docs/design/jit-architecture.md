# JIT architecture: audit and end state

2026-11-12. Audited at master `2dc44d5`. This replaces §3 and §4 of
`jit-redesign.md` (the architecture of record and the M0–M12 milestones).
That document keeps the verified baselines, the evidence, the test plan and
the checked corrections.

The earlier review marked parts of the round-2 external design (ROUND2)
wrong because it assumed machinery the tree does not have: a partial
evaluator, field paths, a flow-sensitive escape pass, effect summaries. It
then set gates at 1.3 to 4 times PyPy. Both were the wrong conclusion.
Missing machinery is work to build, and any module that is not a foundation
for the end state is replaced. The targets are ROUND2's:

| loop, instructions per iteration | now (verified) | target (estimated) | PyPy (verified) | CPython 3.14t (verified) |
|---|---|---|---|---|
| Mako cell | 10,612 | 1,200–1,700 | 1,296 | 4,421 |
| pickle `save` | 43,326 | 1,000–1,400 | 682 | 12,185 |
| Genshi cell | 46,522 | 4,000–5,000 | 2,867 | 19,086 |

Fixed, and nothing else is: CPython 3.14 bytecode as the execution format
with the interpreter as reference and exit target; the cp314t object
layout, reference counts and C-API for anything that reaches C or the heap;
free threading with no global serialization; CPython-observable semantics;
a Python stdlib; no benchmark-specific optimization, behaviour flags or
hard caps; simplicity.

Labels: **verified** (measured, `jit-redesign.md` §1), **source** (read in
the tree at `2dc44d5`), **estimated** (a prediction).

## 1. Verdicts

| layer | files (lines, source) | verdict |
|---|---|---|
| Graph building and the inliner | `ssa_build.c` (6,913) | **Replace the core**: state, joins, build order, inlining, snapshots. Port the per-opcode semantics. |
| IR | `ssa.h`, `ssa.c` (715) | **Keep the form** (block SSA, a value is its instruction, four representations). **Replace the vocabulary**: typed field access, a call instruction, ownership on uses. |
| Optimizer | `ssa_opt.c` (2,685) | **Replace** the seven passes that recover what the builder did not know. **Keep** dominators, loops, guard hoisting, dead code, edge splitting, block order. Add reference placement. |
| Effects of primitives and helpers | flag bits in `ssa.h`, `lucifer.h`; a list in `may_change` | **Replace**. |
| Frame states, exits, materialization | `lu_fs`, `lu_deopt`, `deopt.c` (516), frame fields `jit_desc`, `jit_spills`, `jit_regs` | **Replace**, keeping three rules: an exit lands before an unexecuted instruction, one object per virtual value, released values last. |
| Call protocol and binding | direct entry, `lu_jit_bind`, `lu_jit_run`, `lu_jit_call_finish`, lent frames, 13 kernel twins | **Replace**. |
| Register allocation and code generation | `regalloc.c` (465), `x86_64/emit.c` (3,945) | **Keep** the emitter. **Extend** the allocator (interval splitting) and add the compiled-call convention and a safepoint table. |
| Feedback, tiering, invalidation | `jit.c` (1,958), `deps.c` (218) | **Keep** tiering and the dependency protocol. **Extend** feedback (counts, values). **Replace** the signal to every task. |
| Generators | `inline_resume`, `lu_jit_resume`, resume units | **Replace**: a generator's frame is a frame like any other. |
| Kernel modules written as interpreters | `_struct.py` (342), `_io.py` (3,087), `_codecs.py` (1,001) | **Keep as written.** The evaluator specializes them. The plan to generate per-format packers is dropped. |

The builder core, the virtual-object passes, frame states, exits, the call
protocol and generators are replaced together, as one new middle end. Taken
one at a time they do not come apart: what an inlined frame is, what a
`*args` tuple is and what an exit rebuilds are the same question.

## 2. Audit

### Graph building and the inliner

*Needs.* Graph building as partial evaluation of bytecode, user code and
kernel code alike. The abstract state holds, per path, the contents of
objects the code has made, and each instruction is decided against it
before anything is emitted. `size(args) == 3` on a two-item tuple, `for
key, value in ns_items(kwargs)` over an empty dict, `kind in 'iu'` on a
constant: decided, so the other arm is never built. Inlining is the same
evaluation continued into the callee. Prior art: Graal's partial escape
analysis run during inlining (Stadler, Würthinger, Mössenböck, CGO 2014);
V8 Maglev's graph builder, which keeps known node aspects and virtual
objects while it parses bytecode (`src/maglev/maglev-graph-builder.cc`,
unverified against a pinned revision); PyPy's `optimizeopt`, which does the
same over a trace (`vendor/pypy/rpython/jit/metainterp/optimizeopt/`);
Truffle (Würthinger et al., PLDI 2017).

*Exists.* An abstract interpreter over bytecode already, and the right
kind of thing: it folds a branch on a constant bit, prunes a direction the
interpreter never took, folds a join whose inputs agree, and inlines by
recursion with undo (`translate`, `build_block`, `inline_call`, source).
Its state is three things per local and stack entry: an SSA value, an
owned bit, and an exact type kept in a side table (`bstate`, `known`). It
knows nothing an object contains. So `H_build_tuple` and `H_new_dict` are
emitted at the call (`inline_call`), the callee's `size(args)` is a load
from a real tuple, both arms are built, and a later pass tries to prove
the tuple away over the whole function. Blocks come from a static scan of
jump targets and are built in first-available order; joins get a phi per
variable up front. Budgets are constants in bytecode units (400, depth 4,
3,000 inlines, one opaque call per inline).

*Verdict: replace the core.* Adding the heap to the state changes the
state, the snapshot, every edge and join, the build order (a join needs its
inputs' object states), the inliner and every site that loads from or
passes an object. That is the core, about 1,500 lines. The other 5,000 are
what each opcode, cache shape and kernel idiom means; they port onto the
new state with their tests.

*Not adopted: a trace recorder.* PyPy gets path-specific folding from
concrete execution because its interpreter is the traced program. Here the
interpreter is C; a recorder would be a second implementation of every
opcode and primitive. Evaluation over bytecode with branch counts and tail
duplication at joins (§4.2) reaches the same residual code. The
measurement that would reopen this is in §9.

### IR

*Needs.* Field loads and stores that name their field, so forwarding and
kills work by field, not by helper identity. A call instruction with
callee, convention and summary. Uses that say whether they borrow or
consume a reference. Frame states that name virtual objects.

*Exists.* The container is sound: arrays of instructions and blocks, phis
first, three operands plus an argument list. `HELPER` is one opcode for 98
C functions whose behaviour is two flag bits. `INCREF` and `DECREF` are
emitted by the builder to mirror the interpreter's stack discipline, and
deleting pairs is a later pass's job. A frame state is an array of values
per frame.

*Verdict: keep the form, replace the vocabulary.* The arithmetic, guard,
load, store and branch instructions the emitter lowers stay.

### Optimizer

*Needs.* Little. When the builder evaluates against object contents, a
value is folded, forwarded or virtual when it is built. What is left for
passes over the finished graph: loop-invariant guards, dead code, and
placing the reference operations the residual graph actually needs.

*Exists.* `objects` makes a tuple, instance or iterator virtual only when
every use in the function qualifies, so one use on a cold arm makes it real
everywhere. `escape` does boxes and an empty dict, separately.
`unbox_phis`, `raw_fields` and `virtual_bools` each handle one
representation. `simplify` and `same_loads` re-derive facts the builder
had. The driver runs them in a fixed order with fixed repeat counts.

*Verdict: replace those seven with the builder's state.* A box is a
virtual object with one raw field; so one mechanism covers ints, floats,
bools, tuples, instances and iterators. Keep `cfg_build`, `loops_find`,
`facts` (hoisting), `dce`, `split_edges`, `order_blocks`: about 900 lines
that work on any graph.

### Effects

*Needs.* For each primitive and helper: which fields it reads and writes,
whether it allocates, raises, runs Python, releases a value of unknown
type, parks; and per argument whether it is read, kept, returned or
consumed. For a compiled Python entry, the same, inferred. Prior art:
PyPy's `EffectInfo` (`vendor/pypy/rpython/jit/codewriter/effectinfo.py`):
read and write descriptor sets, an ordered scale from elidable to random
effects, and "forces a virtual".

*Exists.* `IF_PY`, `IF_SEEN`, `LU_METH_NOPY`, `LU_METH_FRAME`, and a list
of ten helper ids in `may_change`. A primitive that only compares two
strings publishes the frame, makes every local real and kills every fact.

*Verdict: replace.* Nothing in the flags is a foundation; the default for
an undeclared primitive stays "anything".

### Frame states, exits, materialization

*Needs.* One description of an activation, read by exits, walkers and the
callee protocol, in which a frame is itself a virtual object. Then a lazy
inlined frame, a virtual `*args`, a sunk allocation, a generator's frame
and an observed frame's identity are one mechanism: a virtual object named
by a state, made by one routine, remembered per activation. Prior art:
Graal's `FrameState` with `VirtualObjectState`; PyPy's virtualizables and
resume data (`virtualizable.py`, `resume.py`); V8's translations with
captured objects; LuaJIT snapshots with sunk allocations.

*Exists.* A good start at run time: `lu_dv` already describes boxes,
tuples, instances and iterators with fields and builds each once per exit.
But `lu_deopt` is a flat copy of every frame per exit; only the root frame
is read lazily; inlined frames are pushed for real around every call that
may run Python, with every local a real object; identity lasts one exit;
allocation failure is fatal in three places.

*Verdict: replace*, with the virtual-object table as the centre.

### Call protocol and binding

*Needs.* A compiled activation that is a machine frame, described by
metadata, with an interpreter frame made only when observed. Arguments in
registers in the representation the caller has (object, raw int, raw
float, fields). Binding decided at compile time. Prior art: HotSpot's
compiled calling convention with interpreter adapters; V8's optimized
frames.

*Exists.* The direct entry pushes and binds a real `lu_frame` as the
interpreter would, behind seven checks, taking a `PyObject **` array: 215
instructions (verified). Rest and keyword arguments are real objects.
Thirteen twins exist to dodge binding.

*Verdict: replace.*

### Register allocation and code generation

*Needs.* At the targets most instructions are inside primitives (searches,
copies, probes), so the backend is not what separates these loops from
PyPy. It needs the new call convention, a table from return address to
safepoint, and for compute code an allocator that does not spill a value
for its whole life because it crosses one call.

*Exists.* Linear scan, one location per value, three callee-saved
registers, direct lowering per instruction, one shared exit stub. Simple
and correct.

*Verdict: keep, extend.* Replacing it now would move no band loop. Revisit
with numbers once the middle end is in.

### Feedback, tiering, invalidation

*Needs.* Counts, not bits, for branches and call sites (arm order,
pruning, budgets). The values that make kernel code foldable arrive from
the caller by inlining, not from the kernel's own polluted caches; the few
that do not (the code object passed to `eval`, a dict result that feeds a
call) need a value profile at the site. Invalidation that reaches only the
units that relied on the fact.

*Exists.* Kind bitsets, four-way callee and class tables, inline caches,
branch direction bits; one tier, retire and escalate; a ticket protocol
for dependencies that is proved and lock-free on the empty path; a writer
then sets a poll bit on every task.

*Verdict: keep, extend; replace the signal.*

### Generators

*Needs.* A generator made and consumed in one region never exists: its
frame is a virtual frame and a yield is a jump. A generator that already
exists is resumed in line with its frame's fields forwarded like any
object's, written back at an exit or an observation. A yielded tuple is
fields at the resume edge. Prior art: PyPy traces through
`resume_execute_frame` with the generator's frame as heap state
(`vendor/pypy/pypy/interpreter/generator.py`).

*Exists.* `inline_resume` loads the frame at entry and stores it at every
yield, only for a generator whose code the builder saw created and whose
body calls no Python. Everything else goes through `lu_jit_resume` in C:
510–619 instructions per item against CPython's 165–192 (verified).

*Verdict: replace with the frame mechanism.*

### Kernel modules written as interpreters

*Needs.* Nothing in the modules. `pack("<B", n)` runs `_struct`'s parser
cache, a generator over the item tuple, a table lookup, a range check and
`int.to_bytes` through a keyword wrapper: 19,846 instructions (verified).
Every one of those steps is decided by the constant format. An evaluator
that follows a constant through a dict value, an attribute written once, a
loop over a constant tuple and a fused generator leaves the range check
and one byte. This is the first Futamura projection; it is what Truffle
does to an interpreter and what PyPy does to `rstruct`.

*Exists.* The modules are plain Python, which is right. The compiler
cannot see through them.

*Verdict: keep as written.* Generated per-format packers (the old M7) were
a workaround for the missing evaluator and are dropped. Where a kernel
routine is written in a way no evaluator can follow, rewrite it in the
kernel.

## 3. Fundamental and inherited

**Fundamental** under the fixed constraints (each stays in the residual
code; costs estimated):

- An exit to the interpreter before an unexecuted instruction, with each
  effect done once. Costs metadata and longer live ranges, no instructions.
- A real cp314t object for anything C, the heap or another thread can
  reach: header, allocator, later free. 40–80 instructions per object over
  a nursery allocation.
- Reference counts on references stored into or acquired from reachable
  mutable holders, and the acquire protocol on such loads.
- The per-object critical section on a reachable mutable builtin: 10–20
  per mutation, uncontended.
- A release that may run arbitrary code happens at its bytecode boundary
  with frames observable.
- One check after a call that can observe frames, a poll per back-edge,
  recursion accounting and a stack check per call that is not inlined.
- Guards on what C may change without telling anyone (the type of a value
  loaded from the heap).
- One atomic transition per resume of a reachable generator.
- One identity per object once anything has observed it.

ROUND2 priced all of this at under 5% of each gap. Measured on this
runtime (2026-11-15, the residual census of the three loops; instructions
inside a compiled loop, verified), the C behind these items costs 3 to 10
times what was assumed above:

| item | assumed | measured |
|---|---:|---:|
| a 2-tuple: build, store, free the previous one | 40–80 per object | 261 |
| a one-key dict: build, store, free | 40–80 per object | 895 |
| `str(7)`: format, allocate, free | | 469 |
| a critical section pair on a reachable builtin | 10–20 | about 110 |
| `d['k'] = x`, existing key | | 337 |

The items stay fundamental; their price is the runtime's to fix, not the
middle end's: an allocation fast path (the small-size path in line, no
collector bookkeeping before an object is tracked), the lock's
uncontended path in line, recursion-depth updates only around calls that
stay out of line, and primitives that do their work without temporary
buffers or out-of-line accessors.

**Inherited**, and removed by the design: an interpreter frame per compiled
activation; lent frames and real locals at publishing instructions; rest
tuples, keyword dicts, item lists and bound methods of calls the compiler
sees; reference operations that mirror the interpreter's stack; escape
analysis over all uses; object-only arguments and returns; helper flag
bits; the fixed inlining limits; critical sections and counted references
on objects nothing else can reach; kernel interpreters interpreted;
generators resumed through C; code with dict namespaces never compiled; a
flat descriptor per exit; a poll signal to every task per invalidation.

## 4. End state

One compiler, one IR, the interpreter as the exit target. Five components
and the backend.

### 4.1 State

```
state   frames, heap, facts            persistent: a snapshot is a pointer
value   SSA value | constant | virtual object, with its exact type if proved
heap    virtual objects:  id -> kind, fields (values), private
        known fields:     (real object, field) -> value
facts   guards proved on this path
```

A frame is a heap entry of kind FRAME. Its locals and stack are its fields
and its caller is a field. There is no separate notion of inlined frame.

A **kind** is one table row: a layout, how to make the real object, and the
operations understood without making it (field load and store, length,
type, truth, iteration, append to a private list). Kinds: int, float and
bool boxes; tuple; list; dict with ordered known keys; instance; cell;
bound method; function made by `MAKE_FUNCTION`; slice; the builtin
iterators; frame; generator; string and bytes as parts (a whole object, a
slice, a fixed-width raw scalar, the ASCII payload of a `str`);
memoryview.

### 4.2 Evaluator

Replaces the builder core and the virtual-object passes. Four rules:

1. An operation the state decides is not emitted. Its result is a
   constant, a field of a virtual object, or an elidable primitive run at
   compile time.
2. An operation a virtual object's kind understands updates the state.
3. Anything else is emitted. A virtual argument is made real first only if
   the operation's summary says it keeps or exposes it. The summary's
   writes kill facts and known fields.
4. A branch the state decides builds one arm. A branch whose other
   direction has a zero count builds a guard.

A call resolves its target (constant, dependency, or guard on the site's
profile), binds through the binding plan symbolically, and then either
continues evaluation in the callee with a new virtual frame or emits a
call to an entry (§4.4). The choice is a budget in residual instructions
weighted by site count; a unit that stays hot is rebuilt with more.

A join gives each variable a phi. A virtual object with the same kind and
layout on every input stays virtual with phis for its fields. Otherwise it
is made real on the edges where it was virtual, or, while the budget
allows, the join's tail is built again for that input. Tail duplication is
how trace-shaped code arises without a recorder.

A loop header starts from its preheader's state with what the loop assigns
(a bytecode pre-scan) generalized; the back-edge state is checked against
it, and on a mismatch the loop is rebuilt from the weaker state. Each
rebuild weakens strictly.

Stable values enter as constants with dependencies: a global, a dict value
(today string keys only; object keys are added for `dispatch[type(x)]`),
and an attribute written once per instance, tracked per attribute in the
shared keys as V8 tracks constant fields (unverified against
`object/dict.h`).

### 4.3 Frame states and the materializer

A frame state is (state, innermost frame, instruction, before or after its
effect). The run-time form is a table per unit, keyed by exit index and by
the return address of each call, into one byte stream: frames with shared
caller chains, virtual objects by kind with a recipe per field (register,
spill word, constant index, another virtual object), and whether compiled
code owns each reference. No raw pointers, so a unit can be cached.

One routine, `materialize(activation, safepoint, object)`, makes an object
from its recipe and records it in the activation's forced table, created
on first use. Every later request returns the same object. Its three
callers:

- **A failed guard.** Makes the frames it names and resumes the
  interpreter.
- **A walker** (`sys._getframe`, a traceback, `PyEval_GetFrame`,
  `sys._current_frames`, `gc.get_objects`). All go through one accessor
  that, on a compiled activation, finds the safepoint from the return
  address and makes the frames asked for. It sets the activation's forced
  word. Compiled code tests that word after a call whose summary says
  frames may be observed and leaves through the ordinary exit, which finds
  its objects in the forced table. A signal-time walker reads code and line
  from the table and makes nothing.
- **A callee** that had to make a caller's object (§4.4).

Allocation failure in the routine raises `MemoryError` at the exit's
instruction, or in the walking thread. Nothing aborts.

### 4.4 Activations, entries and summaries

A compiled activation is a machine frame that begins with the leading
fields of `struct lu_frame` (code, previous, owner) and a forced word. It
is linked into the task's frame chain at entry and unlinked at return:
about six instructions. It has no locals array. Walkers tell it from an
interpreter frame by its owner.

A code object has a table of **entries** keyed by (entry instruction,
shape). A shape gives each parameter and the result a representation:
object, raw int, raw float, or the fields of a virtual kind. The body is
compiled once per shape in use. The generic entry is the shape of all
objects behind a binder for the vectorcall convention; the interpreter and
C enter there. Call entry, loop entry (OSR), generator resume and
namespace code are entry instructions in the same table.

A **binding plan** is computed once per (code, call shape) and read by the
evaluator and by the generic binder.

Every primitive and helper has a **descriptor**: its C function, its
summary (§2, effects) and optionally a transfer function, the rule that
lets rule 1 or 2 apply to it (int to decimal; `to_bytes` of a known width;
concatenation, slice, length and item of parts; UTF-8 of a compact ASCII
`str`; copy of parts into a buffer; a dict probe with a raw int key). An
entry's summary is inferred from its residual graph and published with
the unit. A caller that used it depends on the unit, a new dependency kind
in the existing protocol.

A virtual object passed to an entry that takes its fields stays virtual in
the caller. If the callee's summary says a path may keep it, the entry
returns a second word: the real object if it made one. A caller that sees
one leaves at a guard with that object as the recipe. A callee that leaves
compiled code copies what it made into the caller's forced table and sets
the caller's forced word.

Invalidation marks dependent units dead and nothing else; a unit tests its
own word at its polls and after calls.

### 4.5 Ownership

The evaluator emits no reference operations. A definition is owned (a new
reference) or borrowed from an anchor that outlives it: a parameter, a
constant, a field of a private or immutable holder. A use borrows or
consumes. One pass over the residual graph places retains and releases
(prior art: Perceus, Reinking et al., PLDI 2021; Swift's ownership SSA)
and writes the owned bits into the frame states. A release of a value
whose type may run code is pinned to its bytecode boundary; the rest sink
to the last use or cancel. A load from a reachable mutable holder uses that
holder's acquire protocol in line. A real container nothing else can reach
is filled without its lock.

This relies on two concessions, both already recorded (`jit-redesign.md`
§0, 9 and 10): a count may read lower than CPython's while a holder is
virtual, and an exit that cannot allocate raises `MemoryError`.

### 4.6 Backend

The emitter and its exit stub stay. Added: calls to entries with
arguments in registers, the safepoint table by return address, and
interval splitting around calls.

## 5. Deleted

`ssa_build.c`'s state, edges, blocks, snapshots and inliner; from
`ssa_opt.c`: `objects`, `escape`, `unbox_phis`, `raw_fields`,
`virtual_bools`, `simplify`, `same_loads`; `deopt.c`; `lu_fs`, `lu_deopt`,
`lu_dv`; lent frames, `lu_jit_frames_push` and `_pop`, `GUARD_FRAMES`, the
frame fields `jit_desc`, `jit_spills`, `jit_regs`, `jit_lent`; the direct
entry, `lu_jit_bind`, `lu_jit_run`, `lu_jit_resume`, `lu_jit_call_finish`;
the 13 kernel twins; `HELPER`'s flags, `IF_PY`, `IF_SEEN`, `LU_METH_NOPY`,
`LU_METH_FRAME`, `may_change`; `INCREF` and `DECREF` from the builder;
`INLINE_MAX_UNITS`, `_DEPTH`, `_TOTAL` and the one-call rule;
`signal_all`. Also deleted from the plan: generated struct packers, the
segment calling convention, and projected entries as a separate mechanism.

## 6. Replacing the current tier

Decided 2026-11-12: the current middle end is deleted outright and the new
one is written in its place on master. There is no side-by-side build, no
Makefile variable and no run-time selector, and the old middle end is not
an oracle: nothing is matched against its behaviour.

The oracle is CPython 3.14t. Before the deletion, every conformance corpus
is run with the tier off and the result of each test is recorded
(`results/floor/off/`, written by `tools/floor`). The interpreter's passing
set is the floor: the new tier passes at least that set compiled at first
call, with failing guards, and under the stress lanes, plus `make check`,
`abi-audit`, `check-jit`, `check-harness` and `check-compiler`. What the
evaluator does not yet compile leaves to the interpreter, so coverage
grows without the floor moving. Tests that asserted the old middle end's
internals are replaced by tests of observable behaviour.

## 7. Stages

Sizes are lines of C, estimated; the deletion is counted from the tree.
"Subsumes" names the milestones of the old plan.

| | stage | subsumes | lines |
|---|---|---|---|
| S0 | Committed ladders (`tools/bench/ladder`) and the floor (`tools/floor`) | M0 | 700 (Python) |
| S1 | The spine: state, evaluator, kinds for boxes, tuple, dict, list, instance, iterators and frame; frame states, materializer, walker accessor; descriptors for the primitives reached; deletion of the old middle end | M1, M2, M3 inlined, M4 in part, M12a | +4,500, −11,000 |
| S2 | Coverage: the remaining opcodes and idioms (attribute caches, class construction, closures, handlers, `with`, keyword calls, iteration), loops and hoisting, loop and resume entries, descriptors for all primitives (about 515) with the stress lane that checks them | M4 | +5,000 |
| S2c | The call target from the site's profile (§4.2): a callable that is a value (a parameter, a local) at a site that met one function is guarded to be it and its call continued in line; a bound method object's self is read from it. Then the same for a function known by its code (a closure made anew per call), and the budget spent by site count, not in source order | | +400 |
| S3 | Entries by shape with register arguments and raw ints and floats, generic binder, inferred summaries, unit dependencies, per-unit invalidation, the activation as a machine frame | M3 out of line, M5, M6, M10 | +2,500 |
| S4 | Ownership beyond the evaluator's rule (§10): anchors, private containers without locks, acquire protocols in line | M12b | +600 |
| S5 | Kernel reach: elidable folding, once-written attributes, object-keyed value dependencies, unrolling over constant sequences, string and bytes parts, memoryview, the transfer functions; `bytearray` as a virtual kind, a mutable bytes builder (`_struct._pack` fills one by slices: without it `pack` keeps about 600 per call) | M7, M12c, M12d, M12e | +2,300 |
| S6 | Generators: virtual generator and frame, fusion, in-line resume of an existing generator, yielded fields | M9, M12g | +1,500 |
| S7 | Namespace code: entries for code with exact dict namespaces, virtual namespaces, `eval` inlined on a profiled code value | M8, M12f | +1,200 |
| S8 | Virtual aggregates as entry fields, with the second return word | M12p | +1,000 |
| S9 | Interval splitting in the allocator | | +500 |
| S10 | Persistent unit cache and compiled kernel image | M11 | +1,500 |

About 20,000 lines written against 11,000 deleted. S1, S3, S4, S6 and S8
touch frames, object lifetime and cross-thread state and get the full
review with tests.

## 8. The first slice

S1 proves the spine on the loop that is already one unit. In scope: the
bytecodes and primitives the Mako cell, the call ladder and one AWFY
program (Bounce) reach. Everything in §4.1–4.3 is built for real on that
subset; entries by shape, ownership and transfer functions are not.

Gates, all estimated, against the verified baselines:

- Mako cell 10,612 → ≤ 2,400, with one allocation per cell (the decimal
  string) and no frame pushed. Five searches and three appends still run.
- Inlined keyword call 435 → ≤ 60; inlined `*args, **kwargs` 1,114 → ≤ 60;
  `isinstance(x, (int, float))` 430 → ≤ 10; `str.replace`, no match,
  1,633 → ≤ 260.
- Bounce no more instructions than the current tier.
- From a callee three inlined frames deep: `sys._getframe(n)` identity,
  `f_back`, `f_lineno`, `f_locals` read and write-through, a traceback
  kept past the frame, the same from a C extension, and a virtual tuple
  seen by the walker and again after the exit, each equal to the oracle
  in the `jit` and `jit-deopt` lanes.

If Mako misses 2,400 with the wrappers' objects gone, the survivors report
says where the rest is before S2 starts. If the walker tests cannot be
made to pass without pushing frames, the frame-as-virtual-object design is
wrong and is revisited before anything is built on it.

## 9. Predictions

Instructions per iteration, all estimated, against `jit-redesign.md` §1.
Each stage is judged against its measured parent.

| loop | now (verified) | S1–S2 | then | end | target |
|---|---|---|---|---|---|
| Mako cell | 10,612 | ≤ 2,400 | S4: ≤ 1,900 | S5: ≤ 1,500 | 1,200–1,700 |
| pickle `save` | 43,326 | ≤ 14,000 | S5: ≤ 2,500 | S3, S4: ≤ 1,400 | 1,000–1,400 |
| Genshi cell | 46,522 | ≤ 30,000 | S7: ≤ 12,000; S6: ≤ 6,500 | S4, S8: ≤ 5,000 | 4,000–5,000 |

| operation | now (verified) | end | PyPy (verified) |
|---|---|---|---|
| positional call, not inlined | 215 | ≤ 35 | 98 |
| keyword call, not inlined | 435 | ≤ 40 | 99 |
| `*args, **kwargs`, not inlined | 1,114 | ≤ 60 | 103 |
| generator item, not fused | 510–619 | ≤ 60 | 40 |
| `pack("<B", n)` | 19,846 | ≤ 60 | removed |
| `int.to_bytes(1, "little")` | 2,067 | ≤ 60 | |
| `dict.get`, two str keys | 516 | ≤ 110 | 112 |
| `list.append` | 234 | ≤ 40 | 60 |
| `deque.append`, bound | 379 | ≤ 60 | 14 |

Mako's end number rests on measured parts: 880 instructions of search work
(packet) and the 4,254 emulation (verified). The pickle and Genshi end
numbers are ROUND2's budgets; its itemized lines cover about a third of
pickle's, and nothing measured confirms the rest. They are the target all
the same. The stage that tests them is S5 for pickle and S7 for Genshi,
each of which must remove most of its loop or name what survived.

Measured after the middle end landed, and projected from a census of
what is left per loop (2026-11-15; projections estimated):

| loop | master (verified) | after the stages above | with the runtime's four fixes (§3) | target | PyPy |
|---|---:|---:|---:|---:|---:|
| Mako cell | 6,304 | about 2,170 | about 910 | 1,200–1,700 | 1,296 |
| pickle `save` | 71,921 | about 1,900 (1,500–2,400) | about 1,100–1,800 | 1,000–1,400 | 682 |
| Genshi cell | 63,192 | about 3,500 (3,000–5,500) | about 2,400 | 4,000–5,000 | 2,867 |

The stages alone miss the band on Mako and pickle: what is left is the
C runtime's price per object and per lock (§3), not residual code. The
order by instructions removed (Mako / pickle / Genshi): calls that stay
out of line, by the profiled call target (S2c), entries (S3) and the
budget, 2,000 / 18,000 / 11,000; kernel specialization on constants (S5),
1,300 / 30,000 / 300; generators as frames (S6), 0 / 0 / 18,000; mutable
virtual objects and bytes builders, 0 / 10,000 / 5,000; polymorphic
attribute caches with class-version folding, 124 / 9,000 / 5,300; then
namespace entries (S7, 9,000 on Genshi), reference elision (S4), and
primitive descriptors that drop the frame check and the result-type guard
after a primitive that cannot be observed.

The loops are not the programs: the Mako cell is 49% of its benchmark
(verified). The 122-program geomean stays the headline, and each stage
names programs outside the band in its landing.

What would change the design:

| risk | measurement |
|---|---|
| Joins make objects real that a trace would keep virtual | After S2: instructions spent on objects made at joins, per band loop. Over 10% of a loop after tail duplication: build the recorder experiment over the same state and backend. |
| Compile time and code size grow with deep evaluation (the cli suite is already the worst row) | Compile ms and code bytes per program in the daily, from S1. S10 is the answer for one-shot runs, not a budget constant. |
| An observed frame forces an exit too often (logging, `inspect`, tracebacks kept) | Forced exits per 1,000 calls over the corpus. Over 1: an observation that wrote nothing and kept no frame object is dropped without an exit. |
| A summary is wrong | The stress lane runs a finalizer, a collection and a class mutation at every primitive that claims it cannot be observed. |
| Reference placement moves a release an observer can see | Finalizer and weakref order against the oracle with the stress lane at every moved release. |
| Tools that read frame memory from outside the process see a compiled activation as a header with no locals | True of root frames today. Recorded, not fixed. |

## 10. Implementation decisions

Where the build departs from §4, and why.

**Ownership is decided in the evaluator, not by a late pass (§4.5).** The
state already says, per path, which slots (locals, stack entries, fields of
virtual objects) name each value. Compiled code holds one reference for an
owned value exactly while some slot of the state names it, and releases it
at the instruction where the last slot stops naming it: the bytecode
boundary where the interpreter would drop its last frame reference. So the
stack discipline's pairs are never emitted, releases keep CPython's order
without pinning, and an exit needs no owned bit per entry: each slot takes
a new reference and each owned value the state names is released once. A
value an instruction pops stays held until the instruction ends, and an
exit inside the instruction releases it. A phi takes a reference on each
edge unless its input has no other slot left; a slot a loop carries
unchanged keeps the reference it has, and is one of the slots left. What
remains for S4 is borrowing from anchors and lock elision.

**What compiled code hands to C has the interpreter's count.** The
interpreter's frames hold a reference per slot (it takes one for a
`LOAD_FAST_BORROW` too, where CPython 3.14 takes none), compiled code one
per object. A count can be read only by code that compiled code calls, so
the difference is settled where a call publishes its state: for each
object the call is handed, the unit takes the references the frames'
slots are owed beyond its own before the call and gives them back after
it (`x86_64/emit.c` `holds_of`). An inlined frame counts as the frame the
interpreter would have pushed. A Python callee's parameters take the
holds of the call's own operands for the call's time, as the interpreter
moves its operands into the callee's frame (`helpers.c`
`call_function`). Compiled code's frames are not real until an exit or a
walker makes them, with a reference for every slot: an object they name
that the call is not handed, and the fields of an object not yet made,
have no references of theirs, and such an object's count, reached some
other way, reads lower than in the interpreter. Three more counts differ.
A constant the unit folded (a global, a builtin) carries the unit's
reference while the unit lives and none for its stack entries. A
temporary passed to a function that is not inlined stays compiled code's
until the call returns: one high inside the callee, and released after
the call rather than where the callee lets go of it. What an instruction
makes real from a virtual object keeps the record's references until the
instruction ends.

**Concessions.** Where compiled code may differ from CPython 3.14t, and
nowhere else:

1. A reference count read from C may be lower while its holder is virtual
   (above).
2. A late materialization that cannot allocate raises `MemoryError` at the
   exit or in the walker (§4.3).
3. If an equal int or float is stored into the attribute between a raw
   load and an exit, the identity of the materialized box may differ from
   CPython's.

**Numbers in attributes are numbers.** An instance's inline values hold a
float or an int either as an object or unboxed, a marker beside a raw word
(`object/attrs.c`). A store of a number that does not exist and that
nothing else names writes the raw word (`eval_attr.c`); a load that
compiled code only unboxes reads it (`opt.c` `raw_loads`). Neither makes
an object or takes a reference. A value loaded raw that frame states name
on their stacks is a number that is no object there, and an exit makes one
for the instruction that would have consumed it. Carrying such a value in
a local or into an inlined call, where the program could compare it with
the attribute's own object, is what concession 3 is for: the load then
keeps the object the word held when it held one, and an exit that has to
make the object gives the attribute the same one while the attribute
still holds that number.

**What is known of an object's type is a dependency on its class.** A
class's version covers its attributes and bases; `__class__` assignment
takes back the old class's version as well (`prim/type.c` `set_class`).
The evaluator records the dependency wherever it answers what type a
value of a reassignable class has (`eval_state.c` `lu_ev_type`), and a
unit with dependencies checks that it lives in the check of its frames
after every instruction that may have run code (`GUARD_FRAMES`), so the
running thread's own reassignment or class change is seen before the next
instruction relies on it.

**Locks on attribute stores (S4): the argument, before the code.** A
store to an instance's inline values takes the object's lock byte and
releases it: two locked instructions, the largest single cost of a store
(27% of Bounce's cycles in two stores). The lock serializes writers with
each other, with a reader that installs an unboxed value's box, and with
the move of the values to a dict. Two elisions are sound, on separate
grounds.

*An object that is not published.* An instance the unit made
(`H_new_object`) is private until the unit hands it to anything that can
keep it: an argument of a call or helper, a value stored to the heap, a
return, an exit. While private, another thread can reach it only through
a walker: one that reads this thread's frames (`sys._current_frames`, a
traceback) or one that reads the collector's heap (`gc.get_objects`,
`gc.get_referrers`). A walker of frames runs only while this thread is
stopped at a published call or a poll, and makes the frames real, which
the unit sees in its frame check after the call, before its next
instruction: it leaves, and the interpreter locks. So a private
object's stores need no lock if (a) privacy is a fact of the path, lost
at any use that hands the object on and at every loop header and join
that does not prove it, and (b) every walker that can hand out the object
makes the frames real. (b) does not hold today for the collector's
walkers: `lu_gc_get_objects` and `lu_gc_get_referrers` return tracked
objects without touching compiled activations. Either they make every
task's compiled activations real while the world is stopped, or a private
instance stays untracked until it is published. The first is the
existing mechanism and is the one to build; it belongs with the walkers
in `exit.c`.

*A number over a number, on a published object.* Where the word already
holds an unboxed number and the store writes one, the marker does not
change and only the raw word does. Writers of raw words need no order
among themselves: the last store wins, as with the lock. What the lock
protects is a holder that reads the raw word to copy it elsewhere: the
reader that installs a box, and the move to a dict. Each of those would
have to detect a raw store made after its read: with one barrier on each
side (the writer writes the raw word with an exchange and then reads the
value word again, taking the locked path if it is no longer the marker;
the holder publishes its object or flag and then reads the raw word
again, redoing its copy if it changed), one of the two always sees the
other. That is one locked instruction instead of two, and it changes
every holder in `attrs.c`. It is not built; it needs the race review and
the stress lanes (`threads_unboxed_race` and a writer against `__dict__`
and against `del`) before it is.

**The run-time frame description is a structure, not a byte stream
(§4.3).** One description per exit or call: frames, virtual objects by
state id shared between frames, a recipe per field. The byte stream is a
size and cache concern (S10) and changes nothing the materializer does.

**The safepoint of a call is one store, not a return-address table
(§4.3).** Before a call whose summary says frames may be observed the
unit stores the description's address in its frame record. A lookup by
return address would need frame pointers through every C extension.

**The root activation keeps an interpreter frame record until S3.** A unit
is entered on a pushed frame, as now; its slots hold nothing while the
unit runs. The frame remembers the owner it had (`home`: the
interpreter's, a kernel builtin's, a generator's) and has it again when
the unit returns or leaves: whether a frame is a generator's is a fact of
the frame, which `RETURN_GENERATOR` made, not of its code's flags. Inlined frames are virtual from S1. The evaluator treats the
root as a frame like any other, so S3 changes entry and materialization
only.

**Forcing.** A walker that reaches a compiled activation at a published
call makes every frame of that description: the root gets real slots, each
inlined frame a record off the datastack linked between the root and
whatever ran above it, and each virtual object is made once and entered in
the activation's forced table by state id. The unit tests one word after
the call and leaves through the ordinary exit, which finds the forced
objects by id, moves the forced records onto the datastack and keeps the
locals a walker wrote. An activation that has published nothing shows its
root at its entry instruction and no inlined frames, as the root does
today.

**Loops are rebuilt whole.** A loop header takes its preheader's state
with a phi for each local the loop assigns and each stack entry that is
an object or a number. A virtual object on the stack, or one a slot that
stays reaches, stays virtual round the loop under its id: a field that is
a raw word (an iterator's index) is a phi, and a field a back-edge shows
changing is one in the next build, raw where it carries a number. A
back-edge whose state does not fit (another kind, another length, an
object made in the loop) records what was wrong as a hint for the header
and the unit is built again from the start; there is no undo of partial
graphs. A hint names a carried object by the way to it from the frame's
slots, since ids are a build's own. Each rebuild knows strictly more, so
it ends.

**A join keeps a virtual object the edges changed differently.** When no
edge made it and its records have one layout, each field that differs is
a place of the join like a slot: a raw phi for a number, an object's phi
otherwise, whose inputs are made real on their edges before the phis
exist. An object some edge made, or whose layouts differ, is made on
every edge.

**A generator the unit makes is a virtual frame.** A call of a plain
generator function evaluates its prologue in line; `RETURN_GENERATOR`
makes the frame's slots the fields of a virtual object of kind generator,
with the code unit the frame resumes at and its stack's depth there. A
`FOR_ITER` on one pushes those fields as a frame of the state, with the
`None` a for loop sends, and evaluates the generator's code from the
resume point to its next yield, where the slots become the fields again
and the value is the loop's item, or to its return, which ends the loop.
Round a consumer's loop the resume point is a phi: the header keeps the
code units a back-edge brought (a hint, as for slots), and the step tests
for each. One that escapes is made as it stands (`H_gen_new`); a frame
state inside its body names the generator whose frame it is, and the
materializer runs that frame as the generator's own, above its
consumer's. Only generators whose suspensions are all outside `try` and
`with` blocks are taken, so that dropping one unfinished releases what
its frame holds and runs nothing; one with cell variables, or that
delegates (`yield from`), is a real generator still.

**A unit entered inside a loop leaves at the back-edge of the loop
around it.** Its entry's state (an iterator or generator the interpreter
made) is what its loop header was built for; going round the outer loop
in the same unit would make every later object of the outer loop's body
real at that header. The interpreter takes the outer back-edge and
enters that loop's own unit once it is hot, where what the outer body
makes reaches the inner loop virtual.

**A call in tail position returns through its caller.** Where an inlined
frame's call is followed by `RETURN_VALUE`, each of the callee's returns
goes on through that instruction on its own path instead of joining the
others first, so what one return knows of its value (the constant that
ends a loop, a tuple the consumer unpacks) is known where the value is
used. This is tail duplication of one instruction; the general form is
not built.

**An inlined callee is checked before it is evaluated.** A scan of its
bytecode for instructions the evaluator does not compile decides whether
to inline; what fails later in the callee is an exit from virtual frames,
not an undone inline.

**Exceptions leave first.** In S1 a raising instruction exits to the
interpreter, which unwinds the made frames; handlers inside a unit come
with S2.

**Calls that are not inlined go through the generic call until S3.**
The direct entry is deleted with the old tier; entries by shape replace
it.

## 11. What is built

The new middle end as it runs; the gaps are stated so that the next stage
starts from what is true.

| file | holds |
|---|---|
| `runtime/jit/ssa.h`, `ssa.c` | the IR: instructions, blocks, virtual object records, frame states with binds and held references |
| `eval.h` | the evaluator's interfaces and its rules |
| `eval_state.c` | frames, virtual objects, facts, snapshots |
| `eval_emit.c` | constants, guards, helper calls, making objects real, boxes, releases at an instruction's end |
| `eval_flow.c` | blocks, edges, joins, loop headers and back-edges, hints and rebuilds, the unit's entry |
| `eval_ops.c`, `eval_arith.c`, `eval_attr.c`, `eval_call.c`, `eval_iter.c` | the instructions |
| `eval_prims.c` | transfer functions for the kernel's primitives |
| `opt.c` | trivial phis, dead values, edge splitting, block order |
| `exit.c` | the materializer: exits, walkers (`lu_jit_sync`, `lu_jit_force`), the forced table |
| `helpers.c` | the C compiled code calls, each with its effects |
| `regalloc.c`, `x86_64/emit.c` | kept; exits describe virtual objects by index, calls publish one description |

Virtual kinds: int and float boxes, bools, tuples, lists, str-keyed
dicts, bound methods, list and tuple iterators, instances of the kernel
types whose state is fields (`alloc`, `field_get`, `field_set`), and
frames. Calls inline with positional, keyword, default, keyword-only and
rest parameters; `f(*args, **kwargs)` over a tuple and a dict that do not
exist is the plain call; a kernel function called with a fixed number of
arguments inlines its twin for that number.

What decides how code is specialized:

- **Learned types, by context.** Where the evaluator wants a value's type
  and nothing gives it (`type(x)`, `kind`, an operator's operands, an
  iterator), compiled code reports the type to a record the root keeps for
  that place: the chain of frames from the root, each a code and its
  instruction, hashed. Once a place the unit has run reports again with
  the root knowing more than when the unit was built, the unit leaves and
  is built again, guarding on what was learned. A place that met two
  types, or whose guard keeps failing, is left generic. This is what
  specializes kernel code for where it is inlined: the interpreter's own
  caches in kernel code are shared by every caller.
- **Operators and iteration through the kernel.** `COMPARE_OP`,
  `BINARY_OP`, `CONTAINS_OP`, `GET_ITER` and `FOR_ITER` on operands that
  are not numbers inline the kernel function the interpreter would call
  (`compare`, `binary_op`, `getitem`, `contains`, `get_iter`, `next_item`)
  once every operand's type is known or learned; it folds to what it does
  for those types. A `FOR_ITER`'s step that answers `MISSING` on a path
  ends the loop on that path.
- **Branch proportions.** A conditional jump's cache word counts both
  directions, halving when one is full; a direction taken under one time
  in 64 is not built, and a guard leaves for it.
- **Budget in instructions.** A unit takes calls in line while it has
  fewer instructions than its budget; code the evaluator folds away costs
  nothing. A hot unit that left calls out doubles the budget of its code,
  a bounded number of times.
- **Numbers round a loop.** A local or stack entry that is an int or a
  float on a back-edge is a raw phi with the object it came in as beside
  it; one unbound where the loop starts carries `lu_jit_unbound` there,
  so the first turn needs no guard and an exit before the first store
  still shows the local unbound.
- **Cells.** A known function's cell reads as what it holds now, guarded
  where it is read.
- **Primitives.** A transfer function per primitive where the state
  decides the answer; the exact result type of those whose C always
  returns one; constants of immutable types and of modules, guarded as
  globals are.

Not built yet, each a stage of §7:

- Virtual instances of classes, cells, slices, strings as parts (S2,
  S5). A generator that already exists (a unit entered in its consumer's
  loop, or one a call the unit did not inline made) is resumed through
  C, with the cheap yield and the compiled resume; `yield from` on a
  virtual generator (S6). A virtual object whose layout changes in a loop
  (a list appended to, a dict given a key) exists before the loop.
- Dependencies and hoisting: a global's guards run where it is read.
- Handlers inside a unit: a raise exits. Generators leave at each yield.
- Polymorphic attribute sites take the generic helper.
- Entries by shape: a call that is not inlined is `H_call` into the
  callee's unit through C, about three times the direct entry PyPy makes.
- Tail duplication at joins; unrolling over constant sequences; jump
  threading (a step's `MISSING` test is made again by its caller).
- `tools/jitcheck` lists the growth lanes whose count this middle end
  does not hold flat yet (`NOT_BUILT`): their output is checked, their
  growth reported as a gap, and a lane that stops growing must leave the
  list. The three lanes that read the old tier's why-not counters are
  gone; their programs stay kernel tests.
