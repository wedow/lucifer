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

Branch luc-interp. Stage 0 done (commit "perf(jit): the borrow pass lists each instruction's member uses once"). Next: stage 1 (hooks).
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
