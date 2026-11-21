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


## Notes

**2026-11-21T23:46:26Z**

Review of branch luc-2vj6 (bound frames enter live units, CALL_KW and CALL_FUNCTION_EX share call_vector): no semantic defect found. Differential runs against CPython in plain, jit, jit-deopt (7 and 3) and handoff, base and branch: keyword and default calls whose callee deopts, raises (traceback names), is a generator or coroutine, recursion to RecursionError at limits 200 and 1000 through keyword calls (same depth every run), f(*a) recursion with the limit at 10**6 (RecursionError, no crash), sys._getframe(1) from the callee, __defaults__/__kwdefaults__/__code__ replaced while hot, closures, bound methods and kernel targets through *args/**kw, gen.throw(*args), settrace/setprofile/sys.monitoring switched on mid-loop, and four threads calling while another flips a class attribute the callee reads (unit retirement in flight). Kernel jit_* plus 19 neighbours pass in plain/jit/jit-deopt (trace_opcodes_threads failed under load, passes 10/10 alone on both builds); all jitcheck growth lanes pass, shapes now enforced.

Not landed: null. Whole run n=4, PYTHONHASHSEED=0, one core, min of 7, base -> branch (cycles; instructions): async_tree_cpu_io_mixed 100.51G -> 102.16G (+1.6%), 169.93G -> 170.54G (+0.4%); mdp 198.42G -> 194.32G (-2.1%), 277.52G -> 276.18G (-0.5%); deltablue 4.296G -> 4.288G, 5.575G -> 5.658G (+1.5%); raytrace 9.175G -> 9.134G, 12.644G -> 12.707G (+0.5%); docutils 289.8G -> 286.9G (-1.0%), 305.29G -> 305.75G (+0.2%). Cycle spread was about 10% on a loaded host; instructions are tight and rise on 4 of 5 rows. It fires: lu_eval-entered calls move to the bound path (mdp 36.2M -> 1.0M, async_tree 3.0M -> 0.6M, docutils 7.1M -> 1.5M); compiles +5 or +6 on every row. The saved lu_eval entry is cheap next to frame binding, so the next step is the binding itself (a direct entry by call shape for keyword and default calls) rather than skipping lu_eval; also check whether CALL_FUNCTION_EX now binds generically where lu_call reached the emitted direct entry (deltablue +83M instructions). Branch kept.
