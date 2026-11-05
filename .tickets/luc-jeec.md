---
id: luc-jeec
status: closed
deps: []
links: []
created: 2026-11-04T21:32:01Z
type: task
priority: 0
assignee: Greg Wedow
parent: luc-d5u1
---
# P0b: object layout, allocation and lazy frames under the JIT; Bounce/Richards >=1.5x and NBody >=3x faster than CPython 3.14t, else revisit the design

Spike luc-c1cx (docs/jit-spike.md): JIT equals CPython on Sieve/NBody and is ~2.4x slower on Bounce/Richards; compiled code is 15-27% of samples. In order: (1) shared-key instance layout with inline values and a keys-version guard; (2) inline fast paths for list stores, allocation and float boxing; (3) lazy frame materialization so boxes stay virtual across calls. Bar: Bounce and Richards >=1.5x faster than CPython 3.14t, NBody >=3x, on an idle host. Missing the bar reopens the design. Start after luc-h4gc lands (both touch object layout).


## Notes

**2026-11-05T03:59:48Z**

Include luc-jh5s: on havlak the default JIT (18.7 s) is slower than -X lucifer_jit=off (12.5 s; CPython 3.14 17.4 s) — 20M side exits/run, mostly two opcodes. The JIT must never lose to the interpreter; measure off vs on for every AWFY program in this round.

**2026-11-05T07:04:48Z**

Daily 2026-11-05 (busy host): JIT on vs CPython is worst on CD 12.4x, Storage 11.2x, DeltaBlue 10.9x, Json 6.8x, Havlak 5.3x; geomean 2.64x. Report these five alongside the bar benchmarks; on DeltaBlue, Json and Havlak the JIT is slower than off (luc-jh5s covers the exit mechanism).

**2026-11-05T17:42:17Z**

Round 1 on branch luc-jeec (master 138ebb4 merged), AWFY at canonical inner counts under the exclusive benchmark lock, best of 3 (CPython best of 5; off rows of CD, Mandelbrot, NBody one iteration), ms:

| Benchmark | CPython 3.14t | master on | master off | branch on | branch off | branch on / CPython | branch on / off |
|---|---:|---:|---:|---:|---:|---:|---:|
| Bounce | 1715 | 4005 | 5261 | 1431 | 4264 | 0.83 | 0.34 |
| CD | 2555 | 22317 | 68767 | 9997 | 66494 | 3.91 | 0.15 |
| DeltaBlue | 643 | 2619 | 2578 | 2136 | 2154 | 3.32 | 0.99 |
| Havlak | 3540 | 11783 | 13657 | 9787 | 11437 | 2.76 | 0.86 |
| Json | 878 | 5199 | 5400 | 4629 | 4712 | 5.27 | 0.98 |
| List | 1069 | 3619 | 6066 | 3652 | 6116 | 3.42 | 0.60 |
| Mandelbrot | 1332 | 638 | 148226 | 484 | 145917 | 0.36 | 0.00 |
| NBody | 1950 | 2051 | 175036 | 1103 | 172183 | 0.57 | 0.01 |
| Permute | 2080 | 2340 | 7303 | 1774 | 6257 | 0.85 | 0.28 |
| Queens | 995 | 950 | 2925 | 735 | 2686 | 0.74 | 0.27 |
| Richards | 4274 | 8345 | 15307 | 4783 | 12183 | 1.12 | 0.39 |
| Sieve | 2146 | 2110 | 5796 | 1698 | 4715 | 0.79 | 0.36 |
| Storage | 1728 | 17821 | 22332 | 16317 | 21210 | 9.44 | 0.77 |
| Towers | 1408 | 1969 | 3764 | 1182 | 2869 | 0.84 | 0.41 |
geomean branch on / CPython 1.56
geomean branch on / off 0.24

Bar not met: Bounce 0.83 of CPython's time (bar 0.67), Richards 1.12 (bar 0.67), NBody 0.57 (bar 0.33). JIT on beats off on every row (DeltaBlue 0.99, Json 0.98 are near ties).

Mechanisms, each measured as instructions:u at reduced inner counts against the stage before:
- inline values behind per-class shared names (b2d2386): Towers -30%, Richards -27%, Permute -22%, NBody -21%, Queens -20%, Bounce -15%, Havlak -11%; List +5%.
- identity-equality answers per type version (3e78f07): Richards -17%.
- join fold of agreeing phi inputs (ccd82a8): Bounce -52%, CD -53% (abs() and other kernel dispatch inline again; Bounce entries 2.4M -> 71k per 300 iterations); Storage +5%.
- in-line instance-value loads/stores in compiled code (23a8cb0): NBody -10%, Bounce -10%, Richards -8%, Towers -8%.
- per-carrier float freelist (ec10153): Mandelbrot -22%, NBody -18%.
- per-carrier compact-int freelist (c95cd7f): Bounce -8%, Permute -4%, Towers -4%.
- one-digit ints against wide constants as machine words (1e68c8c): Storage -11%, Havlak -3%, DeltaBlue -2% (as_ssize's _SSIZE_MAX check always deopted).
- cold call sites exit before inlining + recompile once on side exits (e8f05d9): no effect once the join fold landed; reverted (3ed8b14).

What is still between us and the bar (perf, branch binary):
- Richards: calls between compiled units ~30% (eval_frame 9%, lu_jit_enter 4%, frame push/call/pop 8%, vectorcall + h_call 6%, release_refs 3%). Direct calls between units are the next mechanism.
- NBody: float boxing for attribute stores ~30% (PyFloat_FromDouble 10%, _Py_Dealloc 8%, float_dealloc 6%, _Py_NewReference 3%). Compiled code is 53%. 3x needs stores that do not box, i.e. raw doubles in inline-value words: DESIGN 4.5 rules unboxed heap fields out until identity tests justify them; that is the design question this reopens.
- Bounce: int boxing ~14%, list iteration through a critical section (lu_seqiter_next + PyCriticalSection_Begin/End) ~10%, eval_frame 6%.
- Worst rows: Storage 9.4x (list allocation and GC, sequence repetition dispatch), Json 5.3x and DeltaBlue 3.3x (polymorphic sites through generic helpers; the JIT barely beats the interpreter), CD 3.9x.

Gates on the branch with master 389dec7 merged: check-compiler, check-jit, abi-audit, check-harness pass; make check passes (plain, jit 569/569; its jit-deopt lane passed 569/569 on a solo rerun with check-embed after one timeout of kwargs_str_subclass, which hangs intermittently on master too: 7 of 40 runs under -X lucifer_jit=always -X lucifer_stress=deopt:7 time out on master 389dec7, 1 of 20 here).

**2026-11-05T20:39:57Z**

Round 2 on branch luc-jeec2 (master 7f7139e merged), AWFY at canonical inner counts under the exclusive benchmark lock, best of 3 (CPython best of 5; off rows of CD, Mandelbrot, NBody one iteration), ms:

| Benchmark | CPython 3.14t | master on | master off | branch on | branch off | branch on / CPython | branch on / off |
|---|---:|---:|---:|---:|---:|---:|---:|
| Bounce | 1705 | 1449 | 4347 | 900 | 4424 | 0.53 | 0.20 |
| CD | 2534 | 9867 | 66706 | 8225 | 64264 | 3.25 | 0.13 |
| DeltaBlue | 639 | 2179 | 2174 | 1605 | 1999 | 2.51 | 0.80 |
| Havlak | 3498 | 9690 | 11207 | 8045 | 11477 | 2.30 | 0.70 |
| Json | 878 | 4691 | 4689 | 3357 | 4731 | 3.82 | 0.71 |
| List | 1053 | 3749 | 6009 | 1081 | 2045 | 1.03 | 0.53 |
| Mandelbrot | 1330 | 468 | 146645 | 457 | 137208 | 0.34 | 0.00 |
| NBody | 1939 | 1089 | 170158 | 613 | 166516 | 0.32 | 0.00 |
| Permute | 2078 | 1770 | 6296 | 1374 | 6458 | 0.66 | 0.21 |
| Queens | 991 | 722 | 2596 | 645 | 2672 | 0.65 | 0.24 |
| Richards | 4283 | 4954 | 11746 | 2788 | 10097 | 0.65 | 0.28 |
| Sieve | 2136 | 1785 | 4728 | 888 | 4814 | 0.42 | 0.18 |
| Storage | 1717 | 15894 | 21174 | 8863 | 20646 | 5.16 | 0.43 |
| Towers | 1396 | 1210 | 2778 | 957 | 3074 | 0.69 | 0.31 |
geomean branch on / CPython 1.04 (master 1.57); branch on / off 0.17

Bar met: CPython time over ours Bounce 1.89x (bar 1.5), Richards 1.54x (bar 1.5), NBody 3.16x (bar 3). JIT on beats off on every row (closest DeltaBlue 0.80; on master DeltaBlue and Json on were slower than or equal to off).

Mechanisms, instructions:u at reduced inner counts against the stage before:
- calls enter the callee's unit without the eval loop (7a4b8ec): lu_eval runs a pushed frame's unit at its call entry behind a C-entry marker, running a MAKE_CELL/COPY_FREE_VARS prologue itself; h_call binds Python functions and bound methods directly. Richards -12%, Permute -12%, Towers -12%, List -10%, DeltaBlue -6%, Json -6%. A machine-level call (frame pushed by compiled code) was not built: what is left of a call is frame push/pop and binding (see below).
- fix (9b2efd8): a phi of boxes that something made real stays an object phi (p.a = t; p.a is t was False after a loop).
- unboxed float fields (30f469c), DESIGN 4.5 amended: NBody -12%, 1.78x -> 2.60x.
- loop-carried locals unbound at a call's entry stay raw with a bit phi (7ceda63): NBody about -15% time, Sieve about -50% instructions.
- unboxed int fields (8fe7be7): Bounce 1.17x -> 1.87x (with the stages before: Bounce -25%, Permute -13%, Towers -10%).
- unshared list/tuple iterators step without their lock (1cac5fa): Bounce -4%, Storage -3%.
- a staticmethod reached through an instance answered in C (dc648d8): Richards -28%, DeltaBlue -4%.
- call_special of a run-time descriptor as a helper, no *args call (82bb94e): List -62%, Json -18%, CD -3%.
- UNPACK_SEQUENCE of an exact tuple compiled (ce29b51; with the one before): Storage -41%, Json -21%, Havlak -11%, Mandelbrot -11%, DeltaBlue -10%, CD -9%.
- polymorphic inline caches for LOAD_ATTR, keyed by type version, recorded by the generic load (e8b2731): DeltaBlue -5%, Havlak -2%, the rest flat. Stopped there.

What remains (perf, branch binary):
- Richards: calls ~27% (lu_jit_run, h_call, frame push/pop/release). run_task cannot be inlined because a call inside an inlined frame is a side exit; materializing inlined frames at such a call is the next mechanism.
- Storage 5.2x: list allocation and GC.
- Json 3.8x, CD 3.3x, DeltaBlue 2.5x: interpreted code from units retired or codes disabled (type.__call__, __init__ with super() -- LOAD_SUPER_ATTR is not compiled -- Vector.append after 12 compiles), and class attributes of an enum class (a metaclass the specializer does not handle) staying generic.

Gates (master 7f7139e merged): make check pass (577/577 plain, jit, jit-deopt; embed), abi-audit pass, check-compiler pass, check-jit pass (AWFY lanes, difftest always and deopt), check-harness pass.
