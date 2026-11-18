---
id: luc-tgqa
status: closed
deps: []
links: []
created: 2026-11-18T19:01:26Z
type: feature
priority: 1
tags: [jit, perf]
---
# jit: compiled code enters a compiled callee's unit directly

Gap report 2026-11-18 mechanism #2. A call compiled code does not take in line goes h_call -> call_function -> lu_frame_push -> lu_eval -> lu_jit_run -> unit, about 500 instructions of protocol per entry on Richards (7.5 M entries per iteration). f9c8284 dropped the old middle end's direct entry (d6c8996d, f29908ef). Restore it on the new middle end: the call site tests for a Python function (or bound method of one) whose code has a call-entry unit taking exactly the call's positional arguments, and calls that unit's direct entry, which makes the interpreter's call and RESUME checks, pushes and binds the frame in line, links the activation, and runs the unit; the caller pops the frame at return, or hands a frame that left compiled code to lu_jit_call_finish.

## Acceptance Criteria

Richards and the pyperformance rows the gap report names (richards, raytrace, json_loads, pickle/unpickle_pure_python, deltablue, chaos, go) lose instructions per iteration on a core lease; tools/floor quick, kernelcheck plain/jit/jit-deopt and check-jit pass.

## Result

Landed as two commits: direct entries for plain positional codes, then codes with cells (the prologue in C).
Design: a call-entry unit of a code with plain positional parameters (no keyword-only or star ones, not a
generator's) gets a direct entry (target.h struct lu_jit_direct, published first in the code's JIT state with
its argument count; cleared when the unit dies). Compiled code's H_call tests for a function or a bound method of
one and calls the entry with the arguments in place; the entry makes lu_jit_run's checks, pushes and binds the
frame in line (COPY_FREE_VARS in line, MAKE_CELL in C), links the activation in the caller's record and jumps
into the unit's body. The caller then ends the activation, and pops the frame in line (its function and code
released outside the callee's unit, which the code's last reference may free) or hands it to the interpreter
(lu_jit_call_finish). h_call's C calls take the same entry (lu_jit_direct).

Firing (-X lucifer_jit_stats, 3 AWFY iterations / 3+3 pyperformance values, calls compiled code entered from C
before -> after): Richards 22.5 M -> 649, DeltaBlue 11.9 M -> 0.8 M, CD 25.3 M -> 1.4 M, Towers 9.8 M -> 0,
Permute 17.8 M -> 0, Storage 13.1 M -> 352, Havlak 15.6 M -> 8.3 M (before the cells commit); pp richards 940 k ->
16 k, raytrace 2.86 M -> 0.55 M, json_loads 3.96 M -> 0.20 M, pickle_pure_python 6.97 M -> 2.24 M, go 834 k ->
34 k, deltablue 3.98 M -> 0.46 M.
Megamorphic-site microbenchmark: 1,070 -> 650 instructions per call iteration.

Steady instructions / cycles per iteration, core lease, 3 reps, medians, master c503f47 -> this stage:
Richards 0.860 / 0.944, DeltaBlue 0.783 / 0.879, Json 0.888 / 0.971, CD 0.757 / 0.831, Towers 0.760 / 0.816,
Storage 0.837 / 0.879, Permute 0.785 / 0.802, Havlak 0.903 / 0.975, Queens 0.940 / 0.927, List 0.873 / 0.800;
Bounce, Sieve, NBody, Mandelbrot 1.000 instructions (NBody cycles 1.056 at equal instructions: noise).
AWFY geomean 0.880 instructions, 0.919 cycles.
Pyperformance (1 rep; cycles too noisy on pp rows to read): instructions richards 0.800, deltablue 0.810,
go 0.911, raytrace 0.918, pickle_pure_python 0.946, unpickle_pure_python 0.955, float 0.975, hexiom 0.978,
json_loads/nbody/nqueens 0.993, chaos 1.009 (accepted, luc-5y28); geomean 0.938.

Gates: tools/floor quick PASS (626805b alone: FLAKY deopt jit_reclaim_fork, luc-qexw, master has it too),
kernelcheck plain/jit/jit-deopt 776/776, check-jit PASS (GAP lanes unchanged).
Found on the way: a leak of the call's result when the callable's word was among the taken arguments
(tests/kernel/jit_direct_refs.py), fixed before landing; luc-qexw (pre-existing fork-test crash).
