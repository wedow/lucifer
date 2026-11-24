---
id: luc-2r1l
status: open
deps: []
links: []
created: 2026-11-24T14:11:59Z
type: task
priority: 1
parent: luc-orn7
tags: [perf, interp, startup]
---
# Interpreter per-bytecode cost: cold regression, hooks, small ints, exact calls, specialized forms

With the JIT off the interpreter costs 105-114 machine instructions per bytecode against CPython 3.14t's 45-62 (luc-1rwj section A). Every cold path pays it: imports, CLI tools, warm-up, and kernel builtins that run interpreted until hot. Stages: 0 the cold regression between the 11-23 and 11-24 dailies; 1 JIT feedback hooks free when the tier is off and cheap when cold; 2 machine-word int results directly; 3 exact-arguments Python-to-Python call and return; 4 specialized forms by measured frequency and the dispatch sequence.


## State

Branch luc-interp, rebased on master after stage 2. Stages 0-2 landed. Stage A (calls and returns, replacing the
unlanded stage 3 commit) and its fix reviewed; stage B committed for review. Successor order, from the coordinator's brief:

1. B done (count steps inline, locals through the frame, word-sized opcode/oparg). Dispatch is still 10 instructions
   against CPython's 8 (6 plus its instr_ptr store and next_instr step): opcode is live across calls in many handlers
   (SKIP_CACHES, the specialized forms' guards), so gcc stores it to the stack at every dispatch, and keeps one
   register zeroed there; prev_instr/cur_instr are two register moves.
2. C, call_special's forwarding. Census (pyflakes, JIT off, after A): call_special is called 1.87M times, from
   getattr_hook 0.80M and setattr_hook 0.65M with ast.AST's own __getattribute__/__setattr__ slot wrappers (C
   tp_getattro/tp_setattro = the generic ones; CPython's ast.AST has those wrappers too), contains 0.16M,
   getitem 0.12M, get_iter, setitem, next (kernel wrappers of dict, str, tuple...). Each builds a list, a tuple and a
   dict for descr(obj, *args, **kwargs) and the non-kernel wrapper then goes through lu_wrapper_descr_call ->
   call_wrapper (another tuple) -> the C slot: about 8.6% of the run. The largest general lever is not the forwarding
   but the route: an object whose type's tp_getattro/tp_setattro is foreign C (not the kernel's slot functions)
   should have it called directly, as CPython's PyObject_GetAttr does, instead of the kernel hook reaching it through
   call_special and the slot wrapper; then call_special keeps the rare cases, and its *args/**kwargs build (BUILD_LIST,
   LIST_EXTEND, CALL_INTRINSIC_1, BUILD_MAP, DICT_MERGE, CALL_FUNCTION_EX: 2M each) can be weighed again.
3. D, LOAD_GLOBAL_MODULE/BUILTIN (44.5M executions in pyflakes, 59 instructions each, 9.8% of the run after A).
4. E, the remaining forms by the census (POP_JUMP_IF_*, RESUME, IS_OP, attribute forms).

Tools (scratch, untracked; rebuild them from these notes): a counting build is ceval.c with
`lu_opc[kernel][opcode]++` in DISPATCH dumped at exit, and a call census at call_tail's entry (caller kernel or not,
callee kind: function exact/defaults/varargs/keywords, through CALL_FUNCTION_EX, type, METH flags of C functions,
the type of anything else); per-opcode instructions come from `perf record --call-graph fp` with the JIT off, each
sample charged to the opcode label around its innermost eval_frame frame (addr2line, the dispatch table mapping
aliased opcodes to labels, stacked labels to the last), and the callee eval_frame called for a label's share.
Measuring: instructions per bytecode with the JIT off from two sizes of each workload (fib 25/27, the arith loop 1M/2M,
the attribute loop 750k/1.5M; 4.52M, 15M, 15M bytecodes apart, counted with CPython's opcode tracing); whole-run
`perf stat -e instructions:u` with the JIT on for `-c pass`, `-c "import <STARTUP_IMPORTS>"`, pyflakes as the corpus runs it
(PYTHONPATH holding pyflakes, cwd with src/ the vendored CPython), AWFY Richards and DeltaBlue through harness.py;
pinned, one measurement per core lease, two runs per arm.

## Notes

### 2026-11-24 stage 0: the cold rows of the 11-24 daily

Endpoints 8cb18719 (11-23 daily) and ae720dc1, whole-run instructions:

| row | JIT | 8cb18719 | ae720dc1 | change |
|---|---|---|---|---|
| -c pass | off | 154.47M | 154.68M | +0.1% |
| -c pass | on | 173.15M | 173.49M | +0.2% |
| stdlib imports | off | 1565.9M | 1561.7M | -0.3% |
| stdlib imports | on | 1564.3M | 1638.3M | +4.7% |
| pyflakes | off | 33.43G | 33.49G | +0.2% |
| pyflakes | on | 15.82G | 16.82G | +6.3% |

The interpreter and bare startup did not change: the daily's startup rows (python_startup +8%, no-site +10%) were host
load. With the JIT on, two commits (bisected over the 20 runtime commits, every endpoint built the same way):

- 59ea1e23 (list and tuple items read without a count): imports +4.2% (1565.3M to 1629.9M), pyflakes +3.6%. Same
  units, same IR (164 compiles, 25,675 IR instructions); the optimize phase 4.9 to 17.0 ms. Per function: each_ref,
  lu_fs_each_frame, use_bit, live_member, uses_of (+55M of the +64M): the new pass walked every instruction's frame
  state (every value of every inlined frame) on every liveness iteration of every round. Fixed in general form: each
  instruction's member uses are collected once and filtered by the webs still in question; a tuple item's INCREF
  count is one pass. Its decisions are identical (LUCIFER_JIT_DEBUG borrow lines on the five borrow programs).
- 6027746c (class test flag path): pyflakes +5.3% (15.35G to 16.16G), imports unchanged. More isinstance folds
  (builtins with their own __getattribute__ wrapper now fold negatively) reveal more code to inline: 5,604 to 6,281
  inlined calls, IR 289,812 to 322,693, code 40.5 to 45.5 MB, compile time 796 to 925 ms; the extra instructions are
  the compiler's (each_ref, lu_regalloc, lu_fs_each_*, emit). A correct fix whose cost is compile volume: it belongs to
  compiling only what pays back (luc-1rwj cause 2), not to this ticket.

After the fix, against origin/master (718ec7d9), JIT on: -c pass 173.82M both; imports 1629.9M to 1584.7M (-2.8%);
pyflakes 16.69G to 16.24G (-2.7%); Richards (harness 10x100) 120.34G to 120.33G; DeltaBlue (10x12000) 49.96G to
49.63G. Instructions per bytecode with the JIT off unchanged: fib 104.5, arith 113.7, attributes 105.3.

### 2026-11-24 stage 1: feedback hooks

What the interpreter records and what the tier reads: BINARY_OP, COMPARE_OP, STORE_SUBSCR and UNPACK_SEQUENCE
operand kinds (read by eval_arith.c/eval_ops.c to pick the typed form); POP_JUMP_IF_* branch counts
(lu_jit_branch_rare); FOR_ITER iterator kinds (eval_iter.c); CALL's callee tag and table (eval_call.c); class tables
for warm codes (lu_jit_observe_classes, opclasses); PIC notes for codes with units. All were behind a tier-off test
but the branch counts, which were written on every branch with the JIT off. CALL's and CALL_KW's kinds word is read
only as "the site ran" (lu_ev_feedback's cold count), so a call now marks it once; argument kinds are taken only for
a warm code's builtin calls, where the class tables use them. The operator kinds stay per execution until those
operators have specialized forms (stage 4), whose specialization is then the record.

| row | before | after |
|---|---|---|
| fib, instr/bc, JIT off | 104.5 | 100.1 |
| arith loop | 113.7 | 112.4 |
| attribute loop | 105.3 | 104.6 |
| -c pass, JIT on | 173.85M | 170.13M |
| stdlib imports | 1585.0M | 1558.3M |
| pyflakes | 16.24G | 16.19G |
| Richards (harness 5x20) | 12.320G | 12.314G |
| DeltaBlue (harness 5x2000) | 5.95G | 5.93G |

### 2026-11-24 stage 2: machine-word ints

PyLong_FromLongLong (every int result of the eval loop, the kernel's int primitives and compiled code's boxing)
split the value into a scratch digit array and went through lu_long_from_digits (normalize, small-int check again,
alloc, memcpy); it now fills the new int's digits in place. // and % of compact ints ran the general long_divmod and
every int compare walked digit views: compact operands now take machine words with CPython's floor rules
(fast_floor_div/fast_mod). lu_small_int was an out-of-line call per result; both_compact and compact were outlined
too. New test tests/kernel/int_word_edges.py (values at 2^e+-1 for e up to 64, both signs, every operator,
division by zero, bools, a compiled loop whose operands change size).

| row | stage 1 | stage 2 |
|---|---|---|
| fib, instr/bc, JIT off | 100.1 | 97.4 |
| arith loop | 112.4 | 97.4 |
| attribute loop | 104.6 | 96.7 |
| -c pass, JIT on | 170.13M | 170.00M |
| stdlib imports | 1558.3M | 1557.8M |
| pyflakes | 16.19G | 16.17G |
| Richards | 12.314G | 12.314G |
| DeltaBlue | 5.920G | 5.905G |

What is left of int arithmetic is the opcode body: two out-of-line Py_DECREF calls per operator (gcc does not inline
it into eval_frame), the bool and bitwise tests, the switch in lu_long_binary; stage 4's BINARY_OP_*_INT forms.

### 2026-11-24 opcode census, JIT off (lucifer at stage 3)

Executed bytecodes (counting build) and instructions per execution (perf, inclusive of callees):

| | bytecodes | kernel code | instructions | per bytecode |
|---|---|---|---|---|
| stdlib imports | 11.9M | 90% | 1.52G | 128 |
| pyflakes | 273.4M | 90% | 32.26G | 118 |
| fib / arith / attributes | 4.1M / 16.1M / 16.1M | 24% / 6% / 6% | | |

pyflakes, the top rows (opcode: executions, kernel share, instructions each, share of the run). Shared paths, inclusive:
call_tail 35.6%, return_frame 8.5%, call_done 4.0%, call_stack_args 1.9%, push_callee 1.1%, not under the loop 5.6%.

| opcode | executions | kernel | instr each | share |
|---|---|---|---|---|
| LOAD_GLOBAL_MODULE/BUILTIN | 44.5M | 95% | 59 | 8.2% |
| LOAD_FAST(_BORROW) | 51.9M | 91% | 23 | 3.7% |
| CALL (its own part) | 28.7M | 93% | 34 | 3.0% |
| CALL_FUNCTION_EX | 1.95M | 99% | 413 | 2.5% |
| LIST_EXTEND | 1.95M | 100% | 366 | 2.2% |
| CALL_INTRINSIC_1 | 1.94M | 100% | 367 | 2.2% |
| POP_JUMP_IF_FALSE | 14.4M | 91% | 43 | 1.9% |
| LOAD_CONST | 13.0M | 88% | 39 | 1.6% |
| BUILD_LIST | 2.0M | 98% | 234 | 1.4% |
| LOAD_ATTR (generic) | 0.5M | 2% | 806 | 1.3% |
| RESUME | 11.8M | 90% | 34 | 1.3% |
| STORE_FAST | 15.6M | 92% | 25 | 1.2% |
| POP_JUMP_IF_TRUE | 8.0M | 94% | 44 | 1.1% |
| DICT_MERGE | 1.95M | 99% | 146 | 0.9% |
| IS_OP | 15.5M | 99% | 17 | 0.8% |
| TO_BOOL | 7.0M | 79% | 33 | 0.7% |
| FOR_ITER | 1.1M | 18% | 150 | 0.5% |
| BINARY_OP | 0.85M | 33% | 179 | 0.5% |

The imports run has the same shape (call_tail 36.4%, return_frame 8.1%, LOAD_GLOBAL 6.0%, IMPORT_NAME 6.8%).
Nine in ten interpreted bytecodes of these programs are kernel code: the builtins' Python, run interpreted until hot.

### 2026-11-24 stage A: calls and returns, kernel code included

Stage 3 (exact-arguments calls of non-kernel functions) cut fib 97.4 to 82.5 instructions per bytecode but made
pyflakes JIT off 0.8% worse. Per function (s2 against s3, pyflakes, self samples): alloc_frame had become a call (it
had been inlined into lu_frame_push, its only caller; lu_frame_push_exact was a second), +2.2k samples of 320k, and
the failed checks of kernel calls a little more. Kernel code had been excluded for its frames, which count against
kernel_room rather than the recursion limit; lu_depth_room serves both counts.

What CALL meets in pyflakes, JIT off (call census at call_tail's entry, 30.6M calls):

| callee | caller kernel | caller user |
|---|---|---|
| METH_FASTCALL primitive | 12.19M | 0 |
| type(x), bool(x) | 7.98M | 0.01M |
| kernel function, exact positional arguments | 4.38M | 0.01M |
| wrapper_descriptor (2.12M), method_descriptor, method-wrapper | 2.15M | 0.10M |
| kernel function with *args (call_special: 1.87M) | 1.87M | 0 |
| C function METH_FASTCALL\|METH_KEYWORDS (kernel defs: len, isinstance...) | 0.02M | 1.04M |
| user function, exact positional arguments (stage 3's form) | 0 | 0.61M |
| through CALL_FUNCTION_EX | 0.04M | 0.02M |

So "93% of CALLs are kernel code calling kernel functions" was kernel code calling: primitives, type/bool, then
kernel functions. CALL now has, ahead of call_tail and only for an uninstrumented CALL:
- a METH_FASTCALL C function (LU_METH_NOPY allowed) is called directly, the task pinned as lu_call does; its result
  is checked against the pending exception by one compare, _Py_CheckFunctionResult only on a mismatch;
- type(x) and bool(x) of an object whose truth needs no code;
- a function, or a bound method's function with its self put in the free slot, whose code takes exactly the
  positional arguments given (no star parameters, no keyword-only ones) gets a frame in place
  (lu_frame_push_exact, inline: depth and chunk room checked, kernel frames charged to kernel_room), when no
  frame-evaluation hook is set. The native forms of ten kernel functions (lu_native_call) are skipped for direct
  calls of their Python function, which kernel code makes 19k times in pyflakes; calls through their descriptors
  and builtins still take them.
A return skips lu_frame_retire's call when the frame has no frame object.

Test tests/kernel/call_kernel_exact.py: builtins' own calls (str, list, dict, sorted, join, index), their errors,
defaults replaced mid-loop at one site, a builtin shadowed and then rebound mid-loop, another thread replacing a
function's code and defaults between two halves of a loop, a recursive __repr__/__eq__ through builtins to the
recursion limit, sys._getframe and a traceback from a sort key, a profiler switched on mid-loop, generators and a
coroutine. Gate: that test, call_exact_args and 23 call, frame, recursion, profiling and JIT-call programs in
jit-off, plain, jit and jit-deopt, four programs in handoff, jitcheck: all pass but c_recursion_limit, which fails
the same way on master.

pyflakes JIT off, by label (inclusive): CALL and its calls 174.5k of 322.6k samples at stage 3 (54%), 136.1k of
280.3k after (49%); call_tail + return_frame + call_done 48.1% to 25.3% of the run, the rest moved into CALL.
What call_tail still does is mostly lu_wrapper_descr_call (8.6% of the run) and lu_frame_call with bind (2.9%).

| row | master (stage 2) | stage A |
|---|---|---|
| fib, instr/bc, JIT off | 97.4 | 77.4 |
| arith loop | 98.5 | 97.5 |
| attribute loop | 96.7 | 95.7 |
| -c pass, JIT off | 149.49M | 135.22M |
| stdlib imports, JIT off | 1.500G | 1.349G |
| pyflakes, JIT off | 32.03G | 28.02G |
| -c pass, JIT on | 170.19M | 156.36M |
| stdlib imports, JIT on | 1.559G | 1.462G |
| pyflakes, JIT on | 16.18G | 15.22G |
| Richards (harness 5x20) | 12.314G | 12.291G |
| DeltaBlue (harness 5x2000) | 5.916G | 5.703G |

### 2026-11-24 stage A review fix: a frame reads its function's code once

CALL's exact form read func_code three times (its checks; room, depth charge and reference; the frame's code) and
lu_frame_push twice: another thread assigning __code__ in between could store arguments for one code into a frame
sized for another. The code is now read once, its reference taken with a try (lu_try_incref_compare; functions
mark their code shared at creation and assignment so that other threads' tries succeed while it lives), and passed
down. A monitored bool(x) also skipped its C_RETURN event (master's call_tail shortcut lacked the !mon guard).
Tests tests/kernel/call_fast_forms.py (type with a __class__ property, bool's errors, a builtin's own Stack overflow,
a signal raised by a C call in a hot loop, sys.monitoring CALL/C_RETURN/C_RAISE from mid-loop) and
tests/kernel/threads_call_code_race.py (__code__ flipped between a 2- and a 0-parameter code, unsynchronised).
Trying lu_native_call first for kernel callees of the exact form: pyflakes JIT off 28.02G to 28.15G, imports
1.358G to 1.353G; not taken. eval_frame's stack frame is unchanged (0x338 bytes before and after).

### 2026-11-24 stage B: the costs every opcode pays

Py_DECREF/Py_XDECREF and Py_NewRef were out-of-line calls in eval_frame (gcc inlines neither into it); ceval.c now
defines them always inline, the owner-thread step and the immortal test in the loop, _Py_MergeZeroLocalRefcount and
_Py_DecRefShared as calls. GETLOCAL indexed a localsplus local kept on the stack (one reload per access); it indexes
frame->localsplus now. opcode and oparg were ints, sign-extended at each use as an index; they are intptr_t.
Dispatch went from 11 to 10 instructions; CPython 3.14t's computed-goto build (the oracle's eval loop disassembled)
takes 6 per dispatch (one 16-bit load, two byte extracts, oparg moved to its register, the table's address reloaded
from the stack, the indirect jump) plus 2 in each instruction (frame->instr_ptr store, next_instr step).
What is left is above in State.

| row | stage A | stage B |
|---|---|---|
| fib, instr/bc, JIT off | 77.0 | 75.1 |
| arith loop | 97.5 | 96.4 |
| attribute loop | 95.7 | 93.5 |
| -c pass, JIT off | 135.34M | 130.53M |
| stdlib imports, JIT off | 1.353G | 1.302G |
| pyflakes, JIT off | 28.07G | 26.68G |
| -c pass, JIT on | 156.48M | 151.35M |
| stdlib imports, JIT on | 1.466G | 1.432G |
| pyflakes, JIT on | 15.31G | 15.22G |
| Richards | 12.291G | 12.285G |
| DeltaBlue | 5.716G | 5.699G |

Gate: 29 programs (calls, frames, closures, classes, dicts, ints, errors, generators, recursion, JIT calls,
monitoring) in jit-off, plain, jit and jit-deopt, six thread programs in handoff, jitcheck: all pass.

