# JIT spike: does the bet hold?

DESIGN decisions 5, 7 and 9 bet on two things. Builtins written in Python make
the interpreter slower than CPython. A block-SSA JIT that inlines those builtins,
unboxes ints and floats, scalar-replaces what does not escape and elides
refcounts then beats CPython by a wide margin, while every object that reaches C
keeps the exact CPython layout. This spike built the thinnest real slice of that
tier (DESIGN 6.2) and measured it on four AWFY benchmarks.

## What was built

`runtime/jit/` is about 6,600 lines of C. The passes include only
`jit/target.h`, and the backend sits behind it (`x86_64/`, plus `stub/` for
other machines).

- **Trigger and entry.** A per-site counter at `JUMP_BACKWARD` (its cache word)
  and a per-code counter at `RESUME` compile synchronously after 1,000 hits, or
  on the first hit under `-X lucifer_jit=always`.
  - A unit is entered at the boundary the interpreter stopped at: OSR at a loop
    header, or the instruction after `RESUME`.
  - The code object's `co_executors` holds the units.
- **Type feedback.** The interpreter specializes only `LOAD_GLOBAL` and
  `LOAD_ATTR`/`STORE_ATTR`. Operand kinds are now also recorded in cache words
  the interpreter did not use: `BINARY_OP`, `COMPARE_OP`, `STORE_SUBSCR`,
  `CALL` (first two arguments) and `FOR_ITER` (iterator type). A
  `POP_JUMP_IF_*` records the directions it has taken.
- **Block SSA from bytecode** (`ssa_build.c`):
  - an abstract stack that tracks, per entry, whether compiled code holds a
    reference;
  - a frame state at every unexecuted boundary (the donor's BEFORE rule);
  - guards on `tp_version_tag`, dict keys versions, global values,
    `func_version` and cell contents.
- **Inlining** of kernel and user functions with known identity, with the
  callee's frame state chained to its caller's.
  - A callee inlines while the code it compiles stays under 400 code
    units. One larger than that compiles only the branch directions the
    interpreter took (one never taken is a guard, and a guard that keeps
    failing compiles both), so a large function whose taken branches are
    small inlines.
  - `*args` is the tuple of the extra arguments and `**kwargs` a new dict,
    which is never built when nothing reads more than its truth.
  - Kernel module globals fold to constants, except those a kernel function
    rebinds (`global`), which are guarded as user globals are.
  - Primitives become IR: `float_add` becomes `ADD_F`, `m_sqrt` becomes
    `SQRT_F`, `size` becomes a load of `ob_size`, `str_ord` becomes
    `STR_ORD` (a read of the str's data by its kind), and `type_lookup` on an
    immutable type becomes a constant. A bytes item is `BYTES_ITEM`, and two
    exact strs concatenate through a helper, so neither needs a real frame.
  - `call_special(descr, obj, ...)` with a constant descriptor becomes a direct
    call of `descr`.
  - `math.sqrt` inlines through three hidden frames (`function`, `_math_1`,
    `_to_double`); its domain-error branch is a side exit that rebuilds them.
- **Optimizer** (`ssa_opt.c`):
  - trivial phi removal;
  - unboxing of int and float phis;
  - simplification and guard elimination;
  - escape analysis for boxes, where a box nothing observes is virtual and an
    exit builds it once, so aliases stay one object;
  - removal of reference operations on virtual and immortal values;
  - dead code elimination and critical-edge splitting.
  - A box made from an object (an entry or argument guard) *is* that object, so
    identity holds (`x is y`, `list.append(x)`).
- **Backend.**
  - Linear scan over global intervals. Values live across a call get a spill
    word (the donor's "registers are dead across a call").
  - An x86-64 encoder that emits instance-attribute loads, owner-thread
    increfs and decrefs, range iteration, and int and float arithmetic with
    overflow and zero guards in line.
  - The slow paths that allocate or release keep every register.
- **Exits** (`deopt.c`):
  - Each exit is `push index; jmp common`, and the common path saves every
    register.
  - `lu_jit_deopt` rebuilds the root frame and pushes a real frame for each
    inlined call, kernel frames included.
  - An exception exit drops the operands the raising instruction consumed and
    hands the interpreter the error.
  - An exception a handler of the same unit takes, in the root frame or in
    kernel frames inlined into it (which make no traceback entries), does not
    leave: the raising call branches to a landing that adds the root's
    traceback entry and takes the exception, and the handler's
    `PUSH_EXC_INFO`, `CHECK_EXC_MATCH` and `POP_EXCEPT` compile. `RERAISE`, a
    bare `raise`, and a landing while a tool watches exceptions leave.
    `raise` with an exception compiles as a call of the kernel's `do_raise`.
- **Recompilation.** A guard that fails 50 times marks its instruction "do not
  speculate" and kills the units of the root code. A side exit inside an
  inlined copy that keeps firing stops that function from being inlined,
  and every root that still holds the copy recompiles when it leaves there.
  - A unit without loops that leaves by a side exit or an exception on half
    its entries (50 at least) is retired, and its entry stays in the
    interpreter. An exception that leaves the root frame ends the call, so
    it does not count.
  - A compile that finds most of its feedback sites never ran (the first
    call under `always`) is put off: the interpreter runs that entry 50
    times first, once per entry.
- **Shared C answers.** Generic sites call what the interpreter answers in C
  (`lu_subscr_native`, `lu_compare_native`, `lu_native_call`,
  `lu_getattr_native`, `lu_setattr_native`) before the kernel, and
  primitives marked `LU_METH_NOPY` are called from inlined frames too.
  Comprehensions, slices, tuple displays from a starred list, and calls with
  keywords or star arguments compile at the root; inlined frames build,
  iterate, index and slice exact lists and tuples, so the kernel's
  argument parsers over `*args` inline.
- **Globals.** A global load is a constant guarded on the dicts' keys
  versions. A module that binds a new name has its keys versioned again at
  the next compile, as the interpreter's specializer does.
- **Flags.**
  - `-X lucifer_jit=off|on|always` selects the mode.
  - `-X lucifer_jit_stats` prints compiles, exits by reason, side exits by
    opcode, and the functions disabled or retired.
  - `-X lucifer_stress=deopt:N` fails every Nth guard.
  - `-X perf` writes `/tmp/perf-PID.map`.
  - `LUCIFER_JIT_{DEBUG,DUMP,ONLY}` are debugging aids; `DEBUG` names each
    exit (reason, innermost code and pc, opcode) and each failed compile.

## Correctness

| Lane | Result |
|---|---|
| `make check` (JIT on, default thresholds) | pass (536/536 kernel checks) |
| `make abi-audit`, `make check-compiler` | pass |
| difftest, default / `always` / `always` + `deopt:7` | 164/164 each; `make check-jit` runs the last two |
| `make check-jit`: AWFY Sieve, Bounce, NBody, Richards against the oracle in off, on, always, deopt:7 | 16/16 |
| `tests/kernel/jit_loops.py` (exits of every kind), `jit_threads.py` (global, method and attribute changed by another thread mid-loop) | pass in all three modes |
| `check-kernel LANE=jit` / `LANE=jit-deopt` | pass; `make check` runs both (`check-kernel-jit`). The last failures were a call from compiled code that did not poll after it (an interrupt at a call's return), an inlined call that skipped its `RESUME` (tracing turned on just before it), and kernel globals a kernel function rebinds folded to their compile-time value |

AddressSanitizer found most of the builder bugs; the three test programs run
clean under it.

## Performance

AWFY at the canonical inner counts, best iteration with the host otherwise idle (an exclusive benchmark lock) on a
quiet host:
- CPython 3.14.10t and Lucifer with the JIT: best of 5 iterations.
- Lucifer's interpreter: best of 5 for Sieve and Bounce, of 2 for Richards, and
  1 for NBody (a single iteration takes 171 s).

| Benchmark | CPython 3.14t | Lucifer interpreter | Lucifer JIT | JIT vs CPython | JIT vs interpreter |
|---|---:|---:|---:|---:|---:|
| Sieve (3000) | 2.141 s | 5.730 s | 2.155 s | 1.01x slower | 2.7x faster |
| Bounce (1500) | 1.690 s | 5.151 s | 4.006 s | 2.37x slower | 1.3x faster |
| NBody (250000) | 1.940 s | 170.9 s | 1.963 s | 1.01x slower | 87x faster |
| Richards (100) | 4.253 s | 16.87 s | 10.44 s | 2.46x slower | 1.6x faster |

Compile time, from the stats of one run at the inner counts above:

| Benchmark | Compiles | Recompiles | Calls inlined | Compile time | Code |
|---|---:|---:|---:|---:|---:|
| Sieve | 34 | 7 | 14 | 1.8 ms | 75 KB |
| Bounce | 51 | 13 | 35 | 3.4 ms | 193 KB |
| NBody | 43 | 10 | 168 | 4.2 ms | 201 KB |
| Richards | 51 | 11 | 83 | 4.7 ms | 337 KB |

Side exits planted ("bailouts", by opcode, per run):

| Benchmark | Planted |
|---|---|
| Sieve | CALL 9, UNPACK_SEQUENCE 6, CALL_KW, LIST_EXTEND, BUILD_MAP, BINARY_OP, COMPARE_OP 1 each |
| Bounce | CALL 26, UNPACK_SEQUENCE 6, LIST_EXTEND 2, others 1 |
| NBody | CALL 21, UNPACK_SEQUENCE 8, BINARY_OP 4, others 1 |
| Richards | CALL 24, LOAD_ATTR 13, UNPACK_SEQUENCE 6, others 1 |

Exits taken per run:

| Benchmark | Exits taken |
|---|---|
| Sieve | LIST_EXTEND 9,014 and UNPACK_SEQUENCE 7,970 (both kernel code), COMPARE_OP 2,051 |
| Bounce | LIST_EXTEND 2.45 million: `call_special`'s `*args` path, reached when `abs` is called rather than inlined at a site that was cold when `Ball.bounce` compiled |
| NBody | LIST_EXTEND 37,509, UNPACK_SEQUENCE 37,752 |
| Richards | LOAD_ATTR 65,686 (staticmethod and generic attribute lookups) |

**Where the time goes** (perf, `-X perf` map):

| Benchmark | In compiled code | In the runtime's C |
|---|---:|---:|
| Sieve | 15% | 80% |
| Bounce | 21% | 77% |
| NBody | 27% | 71% |
| Richards | 16% | 82% |

The top runtime symbols for each:

| Benchmark | Top runtime symbols |
|---|---|
| Sieve | `PyCriticalSection_Begin` + `End` 19% (each `flags[k-1] = False`), int allocation and free 25% (boxes the escape rule materializes) |
| Bounce | `eval_frame` + `lu_jit_enter` + frame push/pop/call about 20% (calls that were not inlined), attribute stores through `lu_ns_store` with critical sections about 12% |
| NBody | float allocation and free about 20% (every `body.vx = ...` boxes), attribute stores 12% |
| Richards | `probe_keys` + `lu_dict_probe` + `lu_ns_lookup` 24% (the instance-dict shadow check of every method call), `_PyType_Lookup` 9% |

## What held and what did not

**Held:**
- Unboxing, escape analysis and inlining of Python builtins work.
  - NBody's floating-point loop goes through `binary_op`, `float.__add__` and
    `float_add` in the interpreter: 171 s.
  - The JIT runs it in 1.96 s, 87x faster and at CPython's speed, though
    compiled code is only 27% of that time.
  - Kernel methods written in Python inline into machine arithmetic. That is
    decision 7's payoff, now measured.
- Exact-layout objects at every boundary cost nothing extra. Values are raw only
  inside compiled code, boxed once at an exit or an escape, and identity
  survives (a box made from an object is that object).
- The interpreter is a sound deopt landing for inlined kernel frames.
  - Hidden kernel frames, `math`'s closures and user methods are rebuilt at
    exits.
  - Every exit kind (guard, stress, poll, side, error) is exercised by
    `deopt:7`.
- Guards re-checked where they run are enough for correctness under free
  threading without invalidation machinery. `jit_threads.py` changes a global, a
  method and an attribute's type mid-loop from another thread. Recompilation
  after 50 failures keeps a changed site from deopting forever. Dependency
  invalidation (DESIGN 6.2.7) becomes a speed tool rather than a correctness
  one.
- Compile cost is small: 2-5 ms per run, about 0.1 ms per unit.

**Did not hold, or not yet:**
- **"Beats CPython by a wide margin" is not shown.** The slice reaches parity on
  numeric loops and is 2.4-2.5x slower on object- and call-heavy code. The cause
  is not code quality: compiled code is at most 27% of the time. The rest is
  runtime C that CPython 3.14t does in line or avoids:
  - **Instance attributes live in a real dict per instance:** combined tables,
    no shared keys, no inline values.
    - A method call through `LOAD_ATTR_METHOD_WITH_VALUES` must probe the
      instance dict for a shadowing name. CPython checks a keys version instead.
    - Every attribute store is `lu_ns_store` under a critical section.
    - DESIGN 4.1 already allows "attribute values inline behind shared keys,
      with shape-guarded fixed offsets". This is the first thing to build.
  - **Container stores** go through `PyCriticalSection_Begin`/`End`, which are
    function calls. CPython locks the list's own `ob_mutex` with an inline
    fast path.
  - **Allocation:** about 20% of NBody and Sieve. Boxes escape into attribute
    stores (as in CPython) or are kept real because a later Python-running call
    could read the frame's locals. The frame-observation rule is costly: lazy
    materialization of the frame on observation (FOUNDATIONS 2.2,
    `LU_FRAME_JIT`) would remove it. Inline mimalloc allocation and float
    freelists are the other half.
  - **Calls that are not inlined** go through `lu_call` and a nested
    `eval_frame`. There are no direct JIT-to-JIT calls yet.
- **Kernel dispatch code does not fold by plain inlining.**
  - `binary_op`, `compare`, `call_special` (`*args, **kwargs`) and the generic
    getattr loop over tables, take varargs and consult mutable registries.
  - The slice needed three compile-time folds written in C:
    - same-type float operator dispatch to the type's method;
    - `call_special` of a constant descriptor;
    - `type_lookup` on an immutable type.
  - Each restates a fragment of kernel semantics, which decision 7 wants in one
    place. Two remedies: write the kernel's dispatch as small resolver
    functions the JIT can call at compile time (one source for both), or
    teach the inliner varargs and loop unrolling over constant tuples.
- **Feedback is thin.** With only `LOAD_*` specialized, the tier records its
  own kinds.
  - A site that was cold when its caller compiled has no kinds.
  - Bounce's `abs()` was such a site: it fell back to a call whose
    `call_special` path exits 2.45 million times per run.
  - Value or type profiling per call argument, or recompiling when a cold site
    turns hot, is needed.

## Next steps for P0

1. Shared-key instance layout with inline values and a keys-version guard. The
   JIT then loads and stores attributes at fixed offsets with no dict probe and
   no critical section. This is the largest single cost in Bounce, Richards and
   NBody.
2. Inline fast paths for list item stores (`ob_mutex` byte lock), allocation
   (mimalloc page bump) and float boxing. Make `STORE_ATTR` to a known shape a
   compiled store.
3. Lazy frame materialization: a JIT frame owner whose observation (frame
   object, `f_locals`, tracebacks during a call) asks the unit for its values.
   Boxes then stay virtual across calls.
4. Direct calls between units, with frames pushed by compiled code, for
   non-inlined Python functions.
5. Kernel resolver functions for `binary_op`, `compare` and getattr, used by
   both the kernel and the JIT. The three C folds then go away.
6. Recompile on a hot side exit or a newly warm site. Use per-call-site argument
   types as feedback.
7. Background compilation, a perf jitdump, and dependency invalidation that
   removes in-line version guards (DESIGN 6.2 steps 1, 4 and 7).
