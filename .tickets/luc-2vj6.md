---
id: luc-2vj6
status: open
deps: []
links: []
created: 2026-11-21T16:42:43Z
type: task
priority: 1
assignee: Greg Wedow
tags: [perf]
---
# compiled-to-compiled calls go through h_call -> lu_eval -> lu_jit_run

luc-tax0 row 5 (10% mean share): async_tree makes 36M generic compiled calls vs 3.1M direct per run; special methods go through lu_wrapper_descr_call/kernel_wrapper/call_prepended/h_call_ex (27% inclusive in mdp). Build a direct unit-to-unit call: when the callee function has a live compiled unit, guard on the function version and call its entry directly with the frame set up as lu_jit_run does, falling back to the generic path on mismatch or a dead unit; and call a wrapper descriptor's kernel target directly. ~400 LOC. Deopt, generators, recursion budget and tracing/profiling hooks must behave exactly as now. Measure async_tree, mdp, richards-like rows and raytrace.

