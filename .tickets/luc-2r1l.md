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

Branch luc-interp, on master dbb9b594. Stages 0-2, A-E landed; the CALL primitive-path trims, the callee-table fix,
the release-order fix and the return in line committed for review (notes below). Left of (b): the code object's
count and the argcount/flags fold (see the 2026-11-24 note "the trims with the JIT on"). Next: F, then the operand
reference counts (sized below).

(b) Python call and return, pyflakes JIT off, profile of the stage-E build (13.17G instructions; 3.12M exact
Python calls through CALL's in-line push, 6.4M returns). CPython's CALL_PY_EXACT_ARGS + RETURN_VALUE is about 208
together. Here:
- Push, in CALL (4.0% of the run, about 170 per call, then RESUME's 30): bound-method unpacking (two increfs, a
  decref; CPython: the same, as stackrefs); the argument count from the NULL-or-self slot; the code taken with
  lu_try_incref_compare on func_code (an atomic increment on a shared object at every call; CPython reads
  func_version from the cache and holds the code as a deferred stackref, no count); co_argcount, CO_VARARGS/
  CO_VARKEYWORDS, kwonly and the eval_frame hook tested per call (CPython: once, at specialization, then one
  func_version guard); lu_depth_room / lu_depth_take, the kernel-room recursion charge (CPython: a C recursion
  check in _Py_EnterRecursivePy only); the datastack room test (CPython: the same, _PyThreadState_HasStackSpace);
  lu_frame_fill, about 14 stores (code, previous, func, globals, builtins, locals, frame_obj, instr, last, owner,
  ret, jit_desc, jit_forced, stackpointer; CPython's _PyFrame_Initialize stores 9: f_executable, f_funcobj,
  f_globals, f_builtins, f_locals, frame_obj, instr_ptr, stackpointer, owner, plus return_offset and visited);
  the argument copy and a NULL store into every other local (both as CPython); previous, current_frame, LOAD_FRAME
  (CPython: the same three); the dispatch. Lines with the most samples: the try-incref of the code (1928, 0.39%),
  the push (1932, 1.38% with frame.h 325-346 inlined), the method unpacking (1909-1913, 0.6%).
- return_frame (10.3% of the run, about 210 per return): lu_frame_pop is an out-of-line call and 5.5% of the run.
  Inside it: release_refs 28% (a decref of each local and stack slot, CPython's _PyFrame_ClearLocals does the
  same, but here each local is also stored back to NULL, which CPython does not), _Py_ThreadId 20% and Py_DECREF
  12% (frame.c's decrefs are the header's out-of-line ones: one call to _Py_ThreadId per decref; ceval.c has the
  inline lu_decref, frame.c does not), lu_frame_release 10% (the frame_obj test), free_frame 8% (the chunk test),
  lu_frame_unlink 4% (a current_frame compare-and-store CPython does not need: it sets it once), lu_depth_leave,
  and Py_DECREF of the code (CPython: none, the code is a deferred stackref). lu_poll_slow is 1.9% of the run
  under return_frame: the poll after a kernel function returns to user code, which runs the collections the
  allocations scheduled (GC work, not the return's own cost). The in-line part (2.4%): the generator owner test,
  the C-entry test, current_frame, LOAD_FRAME, the SEND test, PUSH_RETURNED's switch on ret (CPython's
  RETURN_VALUE has no return kinds), the kernel-to-user poll test.
  Contained first steps: frame.c's decrefs inline with one thread-id load per frame (about 20% of lu_frame_pop),
  no NULL stores into released locals, lu_frame_pop inlined into return_frame, the code held without a count.

Operand reference counts (its own item; sized 2026-11-24, not built). Today LOAD_FAST and LOAD_FAST_BORROW are
one handler: both take a count (about 9 instructions), and every consumer drops it (about 8-11). The compiler is
CPython's, so LOAD_FAST_BORROW and LOAD_FAST_BORROW_LOAD_FAST_BORROW are already emitted where CPython's
optimize_load_fast (Python/flowgraph.c) proves the local outlives the stack entry: the entry is consumed before the
local is killed and is never stored as a local, except that arguments may arrive borrowed in a callee's locals.
CPython 3.14 marks such words with the stack reference tag (low bit); PyStackRef_CLOSE skips a tagged word; a
borrowed word is made owned where it can outlive its support: RETURN_VALUE and YIELD_VALUE (PyStackRef_MakeHeapSafe),
generator and frame-object snapshots, and every store to the heap goes through PyStackRef_AsPyObjectSteal of an
owned word or a new reference. Deferred words (LOAD_GLOBAL, LOAD_CONST, method loads of objects with deferred
counting) are the same tag on objects only a collection frees; the collector scans thread stacks for them.

Design here, in stages:
1. Borrowed words from LOAD_FAST_BORROW(_LOAD_FAST_BORROW), argument words made owned at the call. A tag bit in the
   value-stack word; ceval.c gets CLOSE (tag test, else lu_decref), AS_OBJ (mask) and OWN (incref if tagged).
   Consumers to convert: every handler that drops or passes an operand (about 240 stack accesses and 216 count
   steps in ceval.c; most become CLOSE or AS_OBJ mechanically); OWN at STORE_FAST/STORE_DEREF (cannot meet a borrow,
   asserted), stores to the heap (STORE_ATTR, STORE_SUBSCR, STORE_GLOBAL/NAME, BUILD_*, LIST_APPEND, SET_ADD,
   MAP_ADD, cells), RETURN_VALUE, YIELD_VALUE, and the moves into a callee's locals (lu_frame_push_exact,
   call_stack_args/call_tail, CALL_FUNCTION_EX packing); C calls get AS_OBJ arrays (the primitive path passes the
   stack slice: a borrowed word needs a masked copy, or the tag test per argument). Other readers: the frame's
   release (CLOSE), exception unwinding to a handler (CLOSE), frame objects (lu_frame_retire's memcpy makes the
   stack part owned; f_locals reads mask), generators (lu_gen_new's memcpy and the saved stack at YIELD: owned),
   instrumentation reading TOP() (mask), PyUnstable_Object_IsUniqueReferencedTemporary (mask), and the JIT: its
   entry from the interpreter (RESUME and back-edge entry read the frame's locals and stack as owned: OWN each
   tagged word on entry), its exits and deopt materialization (exit.c writes owned words: no change), its frame
   states (unchanged: compiled code owns its values). Size: about 350-450 changed lines, mostly ceval.c.
2. Borrowed arguments kept borrowed in the callee's locals (CPython's rule): the release CLOSEs locals, frame
   objects and generators OWN locals when they snapshot, f_locals writes keep the overwritten word (f_overwritten
   already does), the JIT entry OWNs tagged locals. About 100-150 lines.
3. Deferred words: LOAD_GLOBAL of functions and types, LOAD_CONST, method loads push objects with deferred counting
   tagged. The collector must then scan every task's datastack frames, suspended generators' frames and frame
   objects' own frames for tagged words before it frees a deferred object (today it relies on LU_REF_DEFERRED in
   the shared count, and nothing scans stacks). About 150-200 lines plus the collector scan.
Saving (pyflakes JIT off, the census after C2): stage 1 about 39M borrow loads (27.4M LOAD_FAST_BORROW, 5.7M x2 of
the pair) x about 17 instructions = 0.66G (5%), less OWN at calls (3.1M in-line pushes x about 2 arguments x 9 =
0.06G) and the tag test on every owned close (about 100M closes x 1-2 = 0.1-0.2G): net about 0.4-0.5G (3-4%);
stage 2 gives the argument part back (about 0.06G); stage 3 about 26M LOAD_GLOBALs of deferred objects x 17 = 0.45G
(3%). With the JIT on the interpreter's share is smaller (pyflakes about 10.8G): roughly half of these.

F, the remaining forms after C2's census (pyflakes JIT off, instructions each with dispatch, share of the run):
POP_JUMP_IF_FALSE 11.4M x 43 (3.5%) and _TRUE 3.5M x 48 (1.2%): SKIP_CACHES through the opcode table, the
branch-count feedback write (lu_jit_observe_branch) with the JIT off; RESUME 6.8M x 30 (1.4%): the instrumentation
version compare, the poll, the JIT hotness test (CPython's RESUME_CHECK: one eval-breaker compare); TO_BOOL 5.0M
x 32 (1.1%) and IS_OP 9.3M x 15 (1.0%); LOAD_CONST 8.1M x 28 (1.6%); STORE_FAST 9.5M x 16; POP_TOP 2.9M x 42;
NOP/NOT_TAKEN 6.7M x 15 (dispatch only); LOAD_ATTR / STORE_ATTR for classes inheriting a C generic slot
(default_getattribute and the STORE_ATTR specializer, runtime/interp/specialize.c, compare against object's lookup
only): LOAD_ATTR generic 0.5M x 490 (1.7%), STORE_ATTR_FAST 0.37M x 280, DELETE_ATTR 0.21M x 493; COMPARE_OP
0.82M x 119; BINARY_OP 0.85M x 213; FOR_ITER 1.0M x 151. kind and type_lookup's glue go to luc-ee8q (its file
has the per-primitive numbers).

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


### 2026-11-24 stage C1: a C type's generic attribute slots run directly

An ast node class (made by type() over ast.AST, whose dict holds C slot wrappers of PyObject_GenericGetAttr and
PyObject_GenericSetAttr) got the slot thunks, so every attribute read of a node went interpreter -> getattr_hook ->
call_special -> wrapper -> C -> the kernel's generic lookup, and every store from the node constructor the same way
through setattr_hook. Now, as update_one_slot does, a class whose dunders are a base's C slot wrappers of that very
slot gets the wrapped C function as its slot (no __getattr__ for tp_getattro; __setattr__ and __delattr__ wrapping one
function for tp_setattro), so the interpreter's own generic paths and lu_setattr_native take node attributes; and the
kernel hooks run a wrapper of the generic functions as generic access, as slot_tp_getattr_hook does (getattr,
setattr, delattr, getattr with a default, hasattr). getattr_hook runs type's and module's own lookups without
call_special too. pyflakes' output is unchanged (and CPython's).

| row | stage B | C1 |
|---|---|---|
| fib / arith / attribute loop, instr/bc, JIT off | 75.1 / 96.4 / 93.5 | 75.1 / 96.4 / 93.5 |
| -c pass, JIT off | 130.44M | 127.89M |
| stdlib imports, JIT off | 1.285-1.300G | 1.281G |
| pyflakes, JIT off | 26.66G | 14.76G |
| -c pass, JIT on | 151.24M | 149.44M |
| stdlib imports, JIT on | 1.432G | 1.430G |
| pyflakes, JIT on | 15.22G | 10.90G |
| Richards | 12.285G | 12.301G (+0.13%) |
| DeltaBlue | 5.698G | 5.680-5.685G |

Richards does not call getattr; its extra 16M instructions are in JIT code. A probe of getattr() on a class and a
module, 1M iterations: JIT off 4241 -> 4183 instructions per iteration, JIT on 714 -> 733 (the kernel's getattr_hook,
compiled, is two identity tests longer; with the generic test as a primitive call before them it was 871).
Gate: ast_attr_hooks (new) and 13 attribute and ast programs in jit-off, plain, jit and jit-deopt;
threads_ast_attr_hooks (new) and attr_class_assign_threads in handoff; jitcheck: all pass.

### 2026-11-24 stage C1 review fixes

- A subclass of a type with only the old tp_getattr/tp_setattr slots took their stand-ins (getattro_from_getattr,
  setattro_from_setattr) as its specific slots and ran the base's old hooks (tests/capi/ready_run failed on the C1
  build: OldAttrPy's store raised "cannot set other"); the stand-ins are never specific now.
- generic_wrapper takes the type: a wrapper copied into a class that is not a subtype of its d_type is called and
  raises its TypeError, as in CPython (before __getattr__ CPython's hook skips that check, and so does the kernel's).
  type's and module's __getattribute__ copied into other classes no longer take getattr_hook's or lookup_attr's
  direct lookups.
- _type_set and __bases__ assignment take the tags back after rewriting the slots (the class's and its subclasses'),
  so no tag assigned in between names the old slots.
- New cases in ast_attr_hooks (sibling base supplying the hooks, __bases__ and a custom mro(), copied wrappers,
  copied type/module __getattribute__) and threads_ast_attr_hooks (the base's hooks flipped by another thread under
  a hot loop); ready_run: a class over a C type with its own getattro gaining and losing hooks mid-loop.
- Gate: 21 kernel programs in jit-off, plain, jit, jit-deopt; 4 thread programs in handoff; jitcheck; all 16
  check-capi-ext programs: pass. pyflakes after the fixes: JIT off 14.75-14.79G, JIT on 11.05-11.06G (C1 as first
  committed: 14.76G, 10.89G).
- Reviewer's note for stage F: default_getattribute and the STORE_ATTR specializer (runtime/interp/specialize.c)
  still compare against object's lookup, so these classes get no specialised LOAD_ATTR / STORE_ATTR yet.

### 2026-11-24 stage C2: call_special without packing

Census after C1 (pyflakes, JIT off, counting build): 159M bytecodes; CALL_FUNCTION_EX 0.50M, with BUILD_LIST,
LIST_EXTEND, CALL_INTRINSIC_1, BUILD_MAP, DICT_MERGE about 0.5M each (call_special's forwarding: callers contains
0.16M, getitem 0.12M, get_iter 33k, setitem 23k, next 22k, ...), about 5% of the run at the per-op costs above.
call_special(descr, obj, a=MISSING, b=MISSING, /) now calls without packing (no kernel caller passes more than two
arguments after obj); type.__call__ calls __init__ with its *args/**kwargs through bind_special. Test:
special_method_kinds.

| row | C1 (review fixes) | C2 |
|---|---|---|
| fib / arith / attribute loop, instr/bc, JIT off | 75.1 / 96.4 / 93.5 | unchanged |
| -c pass, JIT off | 128.38M | 122.83M |
| stdlib imports, JIT off | 1.268G | 1.212G |
| pyflakes, JIT off | 14.81G | 14.11G |
| -c pass, JIT on | 150.09M | 147.34M |
| stdlib imports, JIT on | 1.424-1.436G | 1.406G |
| pyflakes, JIT on | 11.06G | 10.92-10.94G |
| Richards | 12.285G | 12.271G |
| DeltaBlue | 5.678-5.686G | 5.600-5.603G |

Gate: 14 programs (special methods, descriptors, wrappers, fast paths) in jit-off, plain, jit, jit-deopt; two thread
programs in handoff; jitcheck: pass. Next: D (LOAD_GLOBAL_MODULE/BUILTIN, 25.4M executions in pyflakes after C1).

### 2026-11-24 census after C2 (JIT off, counting build; instructions inclusive of C callees)

| | bytecodes | kernel code | instructions | per bytecode |
|---|---|---|---|---|
| pyflakes | 160.2M | 82% | 14.10G | 88 |
| stdlib imports | 12.0M | 90% | 1.21G | 101 |

pyflakes (opcode: executions, kernel share, instructions each, share of the run); shared paths: return_frame 9.8%,
call_tail 7.6%, not under the loop 2.0%, kernel_call 1.5%, push_callee 0.7%, INSTRUMENTED_RESUME 0.6%,
call_stack_args 0.4%, call_done 0.3%.

| opcode | executions | kernel | instr each | share |
|---|---|---|---|---|
| CALL (with its C callees) | 16.18M | 88% | 265 | 30.4% |
| LOAD_GLOBAL_MODULE / _BUILTIN | 21.36M / 4.81M | 92% | 80 | 14.9% |
| LOAD_FAST(_BORROW) | 27.97M | 84% | 22 | 4.3% |
| POP_JUMP_IF_FALSE | 11.37M | 88% | 43 | 3.5% |
| LOAD_ATTR (generic) | 0.50M | 2% | 490 | 1.7% |
| LOAD_CONST | 8.11M | 80% | 28 | 1.6% |
| RESUME | 6.77M | 83% | 30 | 1.4% |
| BINARY_OP | 0.85M | 33% | 213 | 1.3% |
| POP_JUMP_IF_TRUE | 3.47M | 87% | 48 | 1.2% |
| FOR_ITER | 1.04M | 12% | 151 | 1.1% |
| TO_BOOL | 4.97M | 71% | 32 | 1.1% |
| STORE_FAST | 9.50M | 87% | 16 | 1.1% |
| IS_OP | 9.31M | 99% | 15 | 1.0% |
| POP_TOP | 2.94M | 60% | 42 | 0.9% |
| LOAD_FAST_LOAD_FAST | 5.76M | 87% | 21 | 0.9% |
| STORE_ATTR_FAST | 0.37M | 6% | 280 | 0.7% |
| DELETE_ATTR | 0.21M | 0% | 493 | 0.7% |
| NOP / NOT_TAKEN | 6.65M | 84% | 15 | 0.7% |

The imports run has the same order (CALL 35.4%, LOAD_GLOBAL 11.2%, LOAD_FAST 3.5%, POP_JUMP_IF_FALSE 2.9%,
COMPARE_OP 1.9%, BINARY_OP 1.9%, IMPORT_NAME 1.6%, RESUME 1.5%).

### 2026-11-24 stage D: LOAD_GLOBAL's specialized forms

The forms cost about 60 instructions with dispatch: one handler for both (opcode-dependent selects on the spilled
opcode), the cache skip through the opcode table, str-keys and entry-range tests on both tables, and the site's
sequence checked after the value was taken. Now each form checks the site's words first (lu_site_unchanged), so they
are one specialization's, and a keys table that still has the cached version has the same keys at the same entries:
the guard is the exact-dict test and the keys version (globals; globals then builtins), then the indexed load,
CPython's _GUARD_GLOBALS_VERSION / _GUARD_BUILTINS_VERSION / _LOAD_GLOBAL_*. The try-incref and the keys-pointer
recheck after it stay (a resize between the version check and the take). Cache layout unchanged (the JIT reads it).
Loop of ten loads, each with its POP_TOP, JIT off: module global 91 -> 65 instructions, builtin 108 -> 85.
Also carried: the dead JIT helper h_call_special removed and comments on call_special's old *args fixed (C2 review);
special_method_kinds covers method descriptors, builtins, a raising __get__, bound methods, classmethod and partial
__init__, __new__ returning another class's instance, a metaclass __call__ (dict.get called with no key words its
arity error differently from CPython's clinic message, so the test calls it with its key only).

| row | C2 | D |
|---|---|---|
| fib / arith / attribute loop, instr/bc, JIT off | 75.1 / 96.4 / 93.5 | 72.7 / 96.4 / 93.5 |
| -c pass, JIT off | 122.85M | 119.25M |
| stdlib imports, JIT off | 1.205G | 1.160G |
| pyflakes, JIT off | 14.06G | 13.43G |
| -c pass, JIT on | 147.36M | 144.07M |
| stdlib imports, JIT on | 1.395-1.408G | 1.373G |
| pyflakes, JIT on | 10.90-10.95G | 10.89G |
| Richards | 12.271G | 12.266G |
| DeltaBlue | 5.588-5.603G | 5.578-5.594G |

Gate: global_guard_changes (new) and 12 global, builtin, shared-code and special-method programs in jit-off, plain,
jit, jit-deopt; threads_global_guard_changes (new) and threads_global_churn in handoff; jitcheck: pass.

### 2026-11-24 stage E: dispatch

Dispatch was ten instructions: opcode and oparg loads, next_instr step, cur_instr and prev_instr moves, the target
table base reloaded from the stack, the opcode spilled to the stack, the target load and the jump, and a register
zeroed: `monitored` (the INSTRUMENTED_CALL whose call is being made), NULL at every instruction boundary, which gcc
kept in a register and re-zeroed at every dispatch. It lives in memory now (one load at a call): nine instructions,
CPython's eight. The opcode spill stays: about thirty handlers use opcode after a call (stacked specialized forms,
SKIP_CACHES through the opcode table in 35 places); removing it means a constant cache skip per handler and the
stacked forms taking their opcode before any call, not a contained change.
D review follow-ups: dk_version read atomically by the interpreter and cleared atomically by the dict's writers;
global_guard_changes adds globals turned general-keys mid-loop, a dict subclass as builtins, exec with builtins of
its own changed mid-loop, __builtins__ rebound; the thread program resizes the builtins dict too.

| row | D | E |
|---|---|---|
| fib / arith / attribute loop, instr/bc, JIT off | 72.7 / 96.4 / 93.5 | 70.6 / 94.6 / 91.8 |
| -c pass, JIT off | 119.25M | 117.38M |
| stdlib imports, JIT off | 1.167-1.169G | 1.131G |
| pyflakes, JIT off | 13.44G | 13.11-13.15G |
| -c pass, JIT on | 144.07M | 142.26M |
| stdlib imports, JIT on | 1.384G | 1.374G |
| pyflakes, JIT on | 10.88-10.91G | 10.84-10.85G |
| Richards | 12.266G | 12.263G |
| DeltaBlue | 5.586-5.594G | 5.567-5.581G |

Gate: global_guard_changes and 11 global, builtin, monitoring and profiling programs in jit-off, plain, jit,
jit-deopt; threads_global_guard_changes, threads_global_churn, trace_thread_exit in handoff; jitcheck: pass, except
cprofile_stats, which fails the same way on D (No module named '_lsprof' in this tree's build).

### 2026-11-24 CALL's primitive path

Census split of CALL (pyflakes JIT off, 32% of the run, 16.18M executions): 7.52M go straight to a primitive (all
from kernel code), 3.12M push a Python frame in line, the rest go through call_tail. CALL's own code is 10.65% of
the run: the head 0.75%, the primitive path 5.9% (about 100 instructions a call with two arguments: about 25 to the
call, 14 after it, about 11 per argument decref, about 10 for the decref of the primitive object, about 10 for the
poll, 9 for the dispatch), the in-line Python push 4.0%. Inside primitives: compile() 9.6% (the parser), and the
per-primitive numbers in luc-ee8q's file.
Trims: the kernel primitive objects are immortal (LOAD_GLOBAL's take and CALL's drop end at the immortal test); the
result check tests NULL or a pending exception first (4 instructions on the common path); CALL skips its three cache
words by a constant. The poll stays after every primitive call. A thread-id load hoisted out of the argument
decrefs made lu_decref load the id before its immortal test everywhere (pyflakes off +1.4%) and was dropped.
Tests: exc_cross_run (a C function returning NULL without an exception gives CPython's SystemError at an
unspecialized site; CPython's specialized sites word it differently and do not check a result returned with an
exception set, so the test runs each site once), signal_in_primitive_calls (a timer signal during a loop of builtin
calls runs its handler at the next poll: after the primitive call that is running, or the next RESUME or back-edge),
call_args_finalizers (temporaries whose finalizers call functions, rebind globals and raise).

| row | E | trims |
|---|---|---|
| fib / arith / attribute loop, instr/bc, JIT off | 70.6 / 94.6 / 91.8 | 70.4 / 94.6 / 91.8 |
| -c pass, JIT off | 117.36M | 116.30M |
| stdlib imports, JIT off | 1.148-1.155G | 1.142G |
| pyflakes, JIT off | 13.16-13.17G | 12.99-13.00G |
| -c pass, JIT on | 142.24M | 143.29M |
| stdlib imports, JIT on | 1.380G | 1.386G |
| pyflakes, JIT on | 10.86-10.87G | 10.85-10.88G |
| Richards | 12.263G | 12.264G |
| DeltaBlue | 5.582-5.586G | 5.579G |

The JIT-on startup rows lose 1M (pass) and 6M (imports); not explained yet (the immortal walk over _lucifer's
dict is a few hundred entries).
Gate: the two new programs and 14 call, refcount, signal and global programs in jit-off, plain, jit, jit-deopt;
three thread programs in handoff; jitcheck; exc_cross_run: pass.

### 2026-11-24 the trims with the JIT on; release order; the return in line

Trims with the JIT on: the immortal primitives cost `-c pass` +1.5M and the imports +8.8M (each trim built alone;
the result check and the cache skip were wins in both modes). lu_jit_callee_note named every immortal callable, so
each kernel call site that met a primitive built a callee table, took pic_lock and invalidated, for an entry
speculate_callee never reads (it speculates on Python functions only). Builtin functions are no longer named.
Open, not understood: (1) naming nothing but functions (static types left out too) cost `-c pass` +14.6% and
DeltaBlue +1.6%, so something reads type entries of callee tables or their tags; (2) two interpreter-only edits with
identical behaviour (the result check, the cache skip) move DeltaBlue JIT on by +0.4%: the same units are built,
in a different order (deterministic per binary, different between binaries), the extra in lu_ssa_optimize; look for
anything in the tier's decisions keyed by address or by layout.

Release order (a compatibility fix): CPython's _PyFrame_ClearLocals releases a frame's values from the top of its
stack down to its first local; the interpreter and compiled code's return and unwinding released first to last
(locals, arguments, cells, generators, coroutines). return_release failed in jit-off, plain, jit and jit-deopt
before; the interpreter's release and the JIT's (lu_ev_return, lu_ev_leave_root, throw_to and the error exit) now
go from the top down.

The pop in line: lu_frame_pop, the release and the datastack free are in line (frame.h) with the eval loop's count
steps (lu_decref, now in lucifer.h); the return sets current_frame with a plain store; a popped frame's words are not
stored back to NULL (it has left the stack and has no frame object; a frame object's own frame still clears them,
as f_locals stays readable). Not done: the code's count (a frame holds one because __code__ can be replaced while
it runs, by its own thread too; on the owner thread it is an in-line local step, about 12 instructions for the
pair; removing it needs the collector to find running frames' code, see below) and folding the argcount, flags and
kwonly tests (one more field in the code object, about 3 instructions).

Exact-arguments call and return (`f(i)` loop minus an empty loop, instructions per iteration, JIT off): CPython
3.14t 329, before 401, after 369 (1.12x).

| row | release order | in line |
|---|---|---|
| fib / arith / attribute loop, instr/bc, JIT off | 70.4 / 94.6 / 91.8 | 66.7 / 93.2 / 90.5 |
| -c pass, JIT off | 116.36M | 114.03M |
| stdlib imports, JIT off | 1.141-1.143G | 1.116G |
| pyflakes, JIT off | 12.97-13.00G | 12.63G |
| -c pass, JIT on | 140.93M | 138.76M |
| stdlib imports, JIT on | 1.370G | 1.352-1.353G |
| pyflakes, JIT on | 10.890G | 10.789-10.803G |
| Richards | 12.262G | 12.258G |
| DeltaBlue | 5.605G | 5.591G |

### 2026-11-24 review fixes; compatibility follow-ups for their own ticket

A frame gives its recursion depth back before its references go (CPython's _Py_LeaveRecursiveCallPy precedes the
clear), so their finalizers have it; the release tolerates a frame whose function is already gone. Compiled code's
direct return (direct_done in the x86-64 emitter) still gives the depth back after the unit released its locals:
moving it means the unit's own return sequence, not a few lines.
Found by the review, not fixed here (a separate compatibility ticket):
(a) JIT lanes: lu_ev_settle (runtime/jit/eval_emit.c) appends a dead escape-analysed container's items to the end
of the pending releases, so `a = D('a'); b = [D('b')]` releases a before b in compiled code (CPython: b, a); the
same across inlined frames in throw_to and raised_out.
(b) Exception unwinding releases the value stack after the frame is unlinked and after PY_UNWIND (ceval.c's
exit_unwind and the JIT's raised_out); CPython pops it while the frame is current and before the event
(`def g(): return [D(), 1/0]` with D.__del__ printing sys._getframe(1).f_code.co_name: CPython prints g).
(c) frame_dealloc's order (frame.c) differs from CPython's frameobject.c: code, function and f_locals first, then
the fast locals top down, then f_back.
(d) Not determined: an inlined callee whose frames a walker made real records, returning inside compiled code.
