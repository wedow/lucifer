---
id: luc-ihjn
status: open
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

