---
id: luc-ihjn
status: closed
deps: []
links: []
created: 2026-11-18T20:14:47Z
type: feature
priority: 1
tags: [jit, perf]
---
# jit: an instance made real keeps the numbers the code made unboxed

Gap report 2026-11-18 mechanism #4 (unboxed fields). A virtual instance made real where it escapes (a list, a return, a call) boxed each field the code had computed: chaos 2.2 M floats per 3+3 values, raytrace's _lightIsVisible 1.5 M. lu_ev_instance_real now stores an int or float field the code made, which nothing but the instance names, unboxed (INLINE_STORE_F/_I, as a store of a temporary already does); its first reader that needs an object makes the only one. A field a local also names keeps its object (identity).

## Acceptance Criteria

jitcheck lane fields (tests/kernel/jit_unboxed_fields.py) flat in float boxes; chaos/raytrace float allocations down; gates pass.


## Result

Landed. Float allocations per run: CD 10.1 M -> 0.1 M (3 iterations); pyperformance (3+3 values) raytrace
23.7 M -> 21.4 M, chaos 2.23 M -> 0.92 M. Instructions (core lease, 3 reps, medians) before -> after this
stage: Bounce 0.960, chaos 0.947, Json 0.996, DeltaBlue 0.998, float 0.997, Richards and nbody 1.000.
Accepted losses CD 1.019 and raytrace 1.034, explained: a field read as an object (an argument of a call not
taken in line) has its box made and installed under the owner's critical section (luc-ibnp). With luc-knag
taking more calls in line, the landing has CD at 0.996 and raytrace at 0.973 against master.
