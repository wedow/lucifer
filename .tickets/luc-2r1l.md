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

Branch luc-interp. Stages 0-2 done. Next: stage 3 (exact-arguments calls), started: CALL_PY_EXACT_ARGS/CALL_BOUND_METHOD_EXACT_ARGS in place in op_CALL (lu_frame_push_exact), PUSH_RETURNED tests the plain push first; test tests/kernel/call_exact_args.py.
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
