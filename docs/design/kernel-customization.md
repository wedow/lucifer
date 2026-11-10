# Kernel customization

Status: design, 2026-11-09. Ticket luc-zelv.

## 0. Problem and decision

The kernel (`lib/kernel/*.py`) is a set of generic dispatchers. `binary_op`,
`compare`, `next_item`, `contains`, `setitem`, `sum`, `min`, `max`, `zip` and
`enumerate` are each one Python function that serves every type and every
call site. The tier reaches them three ways, and each defeats specialization:

- **As a C helper** (`h_binary_op`, `h_for_iter`, `h_contains`, `h_setitem`).
  The kernel function runs as its own unit, and that unit is megamorphic,
  because it serves everything.
- **Inlined, but with merged feedback.** An inlined copy reads the kernel
  code's own cache words, which every caller's types have OR'ed into. Worse,
  when one caller's copy fails a guard, `lu_jit_note_exit` marks the kernel
  site `NOSPEC`, or the whole function `noinline`, for every caller.
- **Not at all.** The builder refuses `CONTAINS_OP`, `FORMAT_*`, `STORE_SLICE`,
  set/dict displays and generic subscripts in inlined frames (`root_only`).
  It allows an inlined frame only one opaque call, at depth 1 and not before
  a loop (`opaque_call`). And 130 kernel functions take `*args, **kwargs`.

The decision is **customization by partial evaluation**: types flow into
inlined kernel code from the user site, and the builder folds the kernel's
dispatch on them. Smalltalk got customization the same way. `Object
class>>new` is monomorphic once it is inlined where the class is known
(Smalltalk `optimizing-tier-DESIGN.md` §6.1). Its site records were still keyed
by the method, so they were merged too. The kernel stays the single
statement of semantics, and the builder gains general folds instead of a C
copy of `binary_op`.

## 1. Context-sensitive feedback

**Choice: record the deciding classes at the user site S; specialize K at
compile time; key exit marks by inlining context.** Concretely:

1. **What S records.** Each operator site records the classes of its operands
   (§2), and each CALL site records its callees (this table already exists,
   `lu_callees`). That is everything a kernel dispatcher branches on:
   `type(a)`, `type(b)`, the oparg (a constant), and the callable.
2. **How it reaches K.** The builder guards S's operands against the recorded
   classes and then inlines K. Inside K, `type(x)`, `type_lookup(T, name)`,
   `is_subtype(T, U)`, `kind(x, k)`, `x is y`, a subscript of a constant tuple
   and an `in` on a constant frozenset all fold (§1.1). K's branches then
   fold, and its `call_special(f, ...)` becomes a call of a known function,
   which inlines.
3. **K's own cache words.** These stay merged. They are consulted only where
   no type flows in, such as an item taken from an iterator. Even there they
   choose polymorphic arms (§2.3), never the generic path.
4. **Exit marks by context.** A guard that keeps failing (`NOSPEC`), a hoisted
   guard (`NOHOIST`), and an inlined copy that keeps leaving (`noinline`) are
   all marked in the root code's state, keyed by the chain
   (callee code, call pc) × depth and the failing pc. Today they are marked in
   the kernel code. This applies only when the failing frame is kernel code
   inlined at depth ≥ 1. User code keeps today's per-code marks, since a user
   function usually has one dominant caller and its own unit.
   `lu_jit_nospec(code, pc)` becomes `nospec(ctx, pc)`: it hashes the chain and
   looks it up in the root's table (open addressing, grown on demand). A mark
   made under one caller no longer de-optimizes the other 10,000 callers of
   `binary_op`.

**Rejected: per-site feedback tables keyed by (site, inlined-callee offset),
filled by the interpreter.**
- The interpreter runs K in K's own frame. To key its writes by S, every
  kernel frame would have to carry a context pointer, and every profiled
  kernel instruction would pay a hash lookup.
- The only writers that know the context are the exits of compiled code.
  They can teach only what failed, and item 4 already records exactly that.
- Item 2 covers everything S can decide. What S cannot decide (items in a
  container) is the same for every caller anyway.

### 1.1 Folds the builder gains (all general, none per kernel function)

| fold | condition | guard or dependency |
|---|---|---|
| `type_lookup(T, name)` | T constant, name constant str | none for an immutable type (today). For a heap type, a type-version fact (`GUARD_VERSION` on the constant, then a dependency through `deps.c`) |
| `is_subtype(T, U)`, `kind(x, k)` | both types known | the same version fact (it covers `__bases__` assignment) |
| `a is b`, `a is not b` | both constant, or of known distinct types | none (exists) |
| constant tuple `[i]`, `x, y = t` | t a constant tuple, i a constant int | none |
| `c in C` | C a constant frozenset or tuple of constants, c constant | none |
| `size(t)` | t a constant tuple | none |
| primitive with `LU_METH_PURE` on constants | every argument constant | none: pure on immutable inputs |

`LU_METH_PURE` is one new bit in the primitive descriptor (DESIGN 4.4). It is
set on `type_lookup`, `is_subtype`, `type_tp_name`, `str_join` and
`type_has_sequence`.

## 2. Operators as sends

### 2.1 Feedback

- **Where it is kept.** `struct lu_opclasses` is a side table per code unit
  in `lu_jit_code`, laid out like `lu_callees`. It holds up to four
  `(PyTypeObject *left, PyTypeObject *right)` pairs and a `mega` flag.
  `left` alone serves unary ops and `CONTAINS_OP` (the container).
- **Who writes it.** The interpreter's generic path for `BINARY_OP`,
  `COMPARE_OP`, `CONTAINS_OP`, `UNARY_*`, `STORE_SUBSCR` and the
  `BINARY_OP` subscript. It writes only when an operand's kind bit is
  `OTHER`: int/float/str/list/tuple/bytes sites keep the 8 kind bits they
  have. The cost is a pointer load and at most 4 compares on a path that
  already costs a kernel call. An addition takes `pic_lock` and
  invalidates `LU_DEP_OPCLASSES`, as callee notes do.
- **Lifetime.** Heap types are in a reference cycle (`__mro__`), so only a
  collection frees them. The table therefore names types without owning
  them, and `lu_jit_pic_forget` drops the entries a collection frees. This
  is the PIC rule.

### 2.2 Lowering, per site

1. **Kinds only, both single** (int, float): as today. Int ops are guarded,
   float ops unboxed.
2. **Kinds `INT|FLOAT` on either side** (`s = 0; s += x`, `[1]*n` holding
   floats).
   - **Arms.** Two arms: both int, and otherwise convert each int operand
     with `I2F` and use the float op. For `+ - * /` and the comparisons this
     is exact CPython: `float.__radd__` converts with `PyLong_AsDouble`
     (round-half-even, as `cvtsi2sd` does).
   - **Unboxed phi.** A loop-carried accumulator that has met both kinds
     becomes a float phi after the first float. `unbox_phis` extends to a
     phi whose int input is a constant, or is converted on entry.
3. **Classes recorded, ≤ 4 pairs.** One arm per pair. Each arm:
   - guards `type(a) is L and type(b) is R` (exact type, not version);
   - inlines kernel `binary_op(a, b, op)` with those types known;
   - folds it (§1.1) to the resolved dunder call, plus a run-time
     `r is NotImplemented` test that is usually folded away once the dunder
     inlines and returns a non-NotImplemented constant type.
   The arms join with a phi, through the `path_here`/`join_paths` machinery
   that `load_attr_poly` uses. A value no arm takes leaves (a guard). After
   `GUARD_FAILS` that guard becomes `NOSPEC` under the context, and the site
   then adds the generic arm.
4. **Megamorphic or `NOSPEC`.** A call of the kernel `binary_op` (an opaque
   call, now legal in inlined frames, §3).

### 2.3 `**`

`float ** c` with constant c:

| exponent | lowering | why it is exact |
|---|---|---|
| `2` | `x*x` | `pow(x, 2.0)` is correctly rounded, so it equals the IEEE product |
| `0.5` | `pow` | `sqrt` differs from `pow` at -0.0 and -inf |
| anything else | `OP_POW_F`: a direct C call to `float_pow`'s core, with no Python and no box | the kernel's own core |

The C core keeps CPython's special cases: a negative base with a non-integer
exponent raises `ValueError`, and `0 ** -n` raises `ZeroDivisionError`. Each
reaches the error exit.

`int ** small non-negative constant int` is repeated `MUL_I` with overflow
guards. A mixed int/float operand pair is converted to float first (§2.2).

### 2.4 COMPARE_OP, CONTAINS_OP, unary, subscripts

All of these follow §2.2, through the matching kernel dispatcher:
- `compare`, `negative`/`positive`/`invert`, `truth`;
- `iter.contains`, which folds to `set.__contains__`, `dict.__contains__` or
  `tuple.__contains__` for a recorded exact container;
- `getitem`/`setitem`.

Exact containers get native C helpers (`h_contains` on a set, frozenset, dict
or tuple; a list or bytes slice store) where the kernel method is a loop over
primitives. A C helper is the better intrinsic when its body is one
primitive.

## 3. Always-inline intrinsics; every opcode legal in inlined frames

**Intrinsics** are kernel functions marked by `@intrinsic` in the kernel,
which sets `LU_CO_INTRINSIC` on the code. They cover:
- the operator dispatchers of §2, `len`, `isinstance`, `abs`, `min`, `max`,
  `sum`, `any` and `all`;
- `next_item` and `get_iter`;
- `list`/`dict`/`set` item and contains methods;
- the iteration objects' `__next__` helpers (§4).

An intrinsic call inlines whatever the budget, the depth (a separate depth
count) or a context's `noinline` say. It falls back to a call only on
recursion. The builder charges the budget **after folding**: IR instructions
emitted, not bytecode units. A folded `binary_op` costs what its arm emits,
about 10 instructions, not its 150 units.

**Inlined frames that run Python.** Since luc-hez4 the backend pushes an
inlined frame chain in line, around any `IF_PY` helper called from inlined
code (`push_frames`, `lu_jit_frames_push`), and `GUARD_FRAMES` follows the
call. The builder limits are what is left:
- `root_only()` goes. Every helper that may run Python is emitted with its
  state and `IF_PY`, as `H_call` is. This covers `H_contains`,
  `H_format_*`, `H_store_slice`, `H_build_set`/`H_build_map` with items,
  `H_set_add`, `H_setitem`, `H_get_iter`, `H_unpack` and `H_binary_op`
  (megamorphic).
- `opaque_call()` loses its "depth > 1", "second call" and "loop after"
  refusals. The one-call rule existed because each call pushed frames
  through C. A call now costs a block push. The loop rule (inlined loops keep
  ints boxed) stays only for user callees found by feedback, and is re-checked
  by measurement.
- `MAKE_FUNCTION` in an inlined frame makes the function once, with the
  captured cells as the frame's values. A genexpr or lambda that does not
  escape stays virtual (§4).

## 4. Iteration fusion

`zip`, `enumerate`, `map`, `reversed`, `dict.items`/`keys`/`values`,
`range` (done) and generator expressions fuse in two places:
- **as a `for` loop's iterator** (`FOR_ITER`);
- **consumed by an intrinsic** (`sum`, `min`, `max`, `any`, `all`, `list()`,
  `tuple()`, `set()`, `dict()`, `str.join`, `list.extend`).

These are the mechanisms:

1. **Virtual iteration objects.** A call of `zip(a, b)` inlines its fixed-arity
   twin (§5), which `alloc`s the zip and `field_set`s its iterators. The
   instance is made by `alloc` in the unit and is seen only by `field_get` in
   inlined code, so escape analysis (luc-k5az) keeps it virtual, as it keeps
   instances a lowered class call makes. Its fields are SSA values.
2. **Fused step.** `FOR_ITER` over a value made in the unit inlines that type's
   `NEXT_MISSING` function (`_zip_next`, `_enumerate_next`, `_map_next`) as
   the step:
   - `next_item` on each inner iterator recurses into the same rule, so list,
     tuple and range iterators become `for_step` index phis;
   - the result tuple is virtual, and `UNPACK_SEQUENCE` reads its fields;
   - `_zip_next` and `_map_next` are rewritten without a list per step (§5).
   The iterator is never made, and an exit makes it from its fields, as
   `for_step` iterators are made today.
3. **Genexprs.** A generator from a genexpr whose `MAKE_FUNCTION` is in the
   unit is resumed in line at its consumer's `next_item`
   (`inline_resume`, which exists). That covers a body that calls Python
   once §3 lands (luc-s4lm's first half). The generator object stays virtual
   while only `next_item` sees it (luc-s4lm's second half).
4. **Consumers.** `sum(genexpr)` inlines `sum`'s fixed-arity twin. Its loop
   is `next_item(it)` over the virtual generator, so steps 2-3 fuse it. The
   result: one loop, no iterator, no generator, no tuple per step, and a
   float accumulator in a register.

## 5. A library written for the compiler

1. **Fixed-arity twins.** A kernel function with `*args`/`**kwargs` declares
   twins by arity, for example `@arity(min, 2)` on `def _min2(a, b)`.
   - The builder: a CALL with no keywords and that many positionals inlines
     the twin.
   - The interpreter: its kernel-call path takes the twin too, so cold code
     gains as well.
   - Error paths: a twin raises nothing itself. Where the fast path fails
     (wrong type, a missing method), it calls the full function with the same
     arguments, so messages stay exactly CPython's. The full function stays
     the one statement of keyword and error behaviour.
   - First set: `min`/`max` (2 and 1), `sum` (1 and 2), `str` (1),
     `str.startswith`/`endswith` (1), `bytearray.extend`, `int` (1),
     `zip` (2), `enumerate` (1), `map` (2), `isinstance` with a tuple,
     `getattr` (2 and 3), `hasattr`, `round` (1), `pow` (2).
2. **Dispatchers split by type.** These are rewritten as an exact-type
   fast path plus the generic path:
   - `getitem`, `setitem` and `slice_bounds` return two ints through two
     calls, or use raw-int slice primitives, with no tuple;
   - `_zip_next` fixed for 2 iterators; `_map_next` with one iterator;
   - `isinstance` over a constant tuple of plain classes;
   - in-place `binary_op` builds its error symbol only in `_unsupported`.
3. **What stays out.** These are lever 1, a separate track: `_struct` compiled
   per format, BytesIO/StringIO over a buffer, timsort in C, `_pydecimal` to
   libmpdec, and the `_json` encoder. Customization makes them cheaper but
   cannot close a 17-100× representation gap.

## 6. Code size, compile time, deopt

- **Frames of inlined kernel code.**
  - **Before any effect.** An exit at a guard that precedes any effect of the
    intrinsic (arm guards, type folds) leaves at the user instruction's state
    (the existing `island`). No kernel frame is described or rebuilt, and
    the interpreter re-runs the operator. This covers almost every guard in
    §2.
  - **After an effect** (a dunder ran, an item was consumed), the exit
    rebuilds the kernel frames as `deopt.c` does today, because the
    interpreter must resume mid-kernel. Hidden frames are hidden from
    walkers, not from resumption.
- **Budgets.** Every budget is adaptive, with no cliffs:
  - **Charged after folding**, in IR instructions.
  - **Unit budget.** Each unit gets a budget proportional to its hotness
    (entries × loop trips seen at compile), with a floor of today's 3000.
    Past it, inlines continue for intrinsics and for sites whose count is
    above the unit's median, so the budget degrades gradually.
  - **Polymorphic arms.** Up to 4 per site, then the generic arm. 4 is the
    feedback table's width, not a limit on the program.
- **Compile time.** Folding happens during the build: a folded branch is never
  built. A folded `binary_op` costs less to compile than today's `float_binary`
  path, which inlines `float.__add__` through a kernel method lookup.
  Compile time is measured per stage (`-X lucifer_jit_stats` phase seconds,
  cli.black is the canary).
- **Deopt storms.** A context mark that fails in another context does not
  fire there. A unit retired under one context recompiles with that context's
  marks only. The exit-storm defects in the report (`abs(-v)`, the
  unpack_sequence hoisted guard) belong to luc-exitfix, which runs in
  parallel. This design only avoids adding new ones.

## 7. Stages

Each stage lands alone, on the micro-kernels and the 15 programs,
with all five gates green. A stage that does not move its rows is reverted.

| stage | content | rows it must move |
|---|---|---|
| 1 | Context-keyed exit marks. Class feedback at operator sites. The folds of §1.1. `binary_op`/`compare`/unary as inlined, folded dispatch with ≤4 arms. INT\|FLOAT arms. `**` lowering. The lazy error symbol | k_user_add, k_int_acc_float, k_float_pow(_const), k_abs_neg; raytrace, chaos, spectral_norm, nbody, telco, go |
| 2 | Every opcode legal in inlined frames, `opaque_call` limits lifted, intrinsics always inline, budget after folding | k_in_frozenset_type, k_fstring, k_slice_assign, k_isinstance_tuple; deepcopy, fannkuch, pickle, unpickle, telco |
| 3 | Fixed-arity twins and split dispatchers (§5) | k_min_max, k_str_int, k_startswith, k_bytearray_extend, k_sum_list, k_getattr, k_hasattr, k_ord_chr; telco, pickle |
| 4 | Iteration fusion (§4) | k_zip, k_enumerate, k_dict_items, k_map_lambda, k_sum_gen, k_join_gen, k_set_genexpr, k_tuple_genexpr; spectral_norm, nqueens, pyflate |
| 5 | Container intrinsics: exact set/frozenset/tuple `in` in line, list/bytes slice store native, list unpack without a tuple copy, `str[i]` without boxing | k_str_index, k_list_unpack, k_slice_assign; fannkuch, nbody, nqueens |

**Why this order.**
- Stage 1 is the largest share on the programs: `binary_op` is 79% of
  raytrace, 67% of spectral_norm, 59% of chaos and telco, and 42% of nbody. It
  needs no frame work.
- Stage 2 is the precondition for 3 and 4: twins and fused steps call Python
  from inlined frames.
- Stage 3 before 4: fusion inlines `sum`/`zip`/`enumerate` through their
  twins.
- Stage 5 is independent and small, and can run whenever a slot is free.

**Measurement per stage.**
- The 67 micro-kernels, `micro.py NAME 15`, one process each, on the bench
  host's pinned core, against the CPython 3.14t column.
- The 15 programs through `tools/bench/corpus time` against the daily's
  reference.
- The pyperformance, cli and AWFY geomeans.
- Flask in-process (the test client, best of 3 × 2000 per route).
- Instructions under the core lease wherever timing is noisy.
