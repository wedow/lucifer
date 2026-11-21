---
id: luc-tax0
status: open
deps: []
links: []
created: 2026-11-21T15:19:58Z
type: task
priority: 1
tags: [perf, jit]
---
# perf: where steady-state realistic code loses to CPython (cross-program attribution)

Attribution of Lucifer's steady-state time on 12 pyperformance rows spread over the distribution (regex_v8 7.0, mdp 8.6, async_tree_cpu_io_mixed 6.7, 2to3 5.8, docutils 3.6, json_loads 3.9, unpickle 3.3, django_template 2.3, deepcopy 2.5, tomli_loads 1.5, pathlib 1.65, deltablue 1.2; logging and sqlite_synth too short to separate) plus cli.pygments cold. Mechanisms ranked in the notes.


## Notes

**2026-11-21 (method)**

Each row ran twice per runtime under `perf record -e cycles:u,instructions:u --call-graph=lbr` (Lucifer with -X perf so units are named), as `run_benchmark.py --worker -l L -w 1 -n N` with L a quarter of the status run's loops: N=1 and N=4. Steady state is the difference (three more values after warm-up), so start-up and first compiles cancel; the steady Lucifer/CPython cycle ratio per row came out at regex_v8 7.6, mdp 7.7, async_tree_cpu_io_mixed 7.3, 2to3 5.9, json_loads 4.0, docutils 3.8, unpickle 3.6, django_template 3.0, tomli_loads 2.0, deepcopy 1.9, pathlib 1.7, deltablue 1.7, close to the status rows. Leaf samples are bucketed by the source file of the symbol (nm -l); samples under the interpreter, allocation and lookup are also charged to the innermost py:: frame of the LBR chain. -X lucifer_jit_stats and LUCIFER_JIT_DEBUG at N=1 and N=4 give the steady compiles. Share = fraction of Lucifer's steady cycles; summed over the 12 rows (each row weighs 1).

**Mechanisms, ranked by summed share**

| # | mechanism | summed (mean) | rows >=5% / >=10% | evidence | general fix | size |
|---|---|---|---|---|---|---|
| 1 | time inside compiled units | 3.37 (28%) | 12 / 11 | kernel builtins compiled generically for megamorphic operands: tuple.__eq__ (kernel compare loop, a prim call per element) 17% of mdp, dict.__setitem__ 6% of unpickle, binary_op/compare dispatch with type_lookup prim calls (math._odd_product + int.__mul__ 25% of async_tree) | customize kernel builtin bodies per receiver type at the call site (type-split inlining), and lower the trivial prims (seq_get, size, dict probe, set probe) to IR loads | 600-1000 LOC; prim lowering 100-150 per family |
| 2 | allocation, free, refcount, collector | 1.59 (13%) | 12 / 9 | objects CPython never makes: *args/**kwargs packing in non-inlined kernel paths (type.__call__ makes a list, tuple and dict per instantiation; call_special the same per special-method call; str _args), boxed ints in generated matchers (1.5M int at <sre> _entry per value) | forward a function's own star parameters without packing (CALL_FUNCTION_EX of unmodified *args/**kwargs passes the caller's vector), then escape analysis across non-inlined calls | ~250 LOC forwarding |
| 3 | native builtins/prims called from compiled code | 1.39 (12%) | 12 / 5 | prim_seq_get, prim_set_probe, prim_dict_find/probe, PyLong_FromLongLong boxing via h_prim_call; lu_str_from_utf8 20% of unpickle (byte-at-a-time decode) | same lowering as 1; ASCII fast path in lu_str_from_utf8 | 40 LOC for UTF-8 (one row) |
| 4 | JIT compile in steady state | 1.34 (11%) | 7 / 5 | 15-90 compiles per three values (deepcopy 15 = 610 KB code, deltablue 46 = 1.8 MB, regex_v8 91, django 14): recompiles after guard failures at a doubled inline budget, retire/recompile cycles climbing to level 9 (chain.__next__, iterator_next); units average 28-40 KB; 60% of compile time is frame-state walking (each_ref 11%, lu_ev_virt 6%, lu_fs_each_frame 5%, stays_virtual 5%, stamp_* 7%, next_differing 3%, walk_base 3%) that re-walks every inlined caller frame and virtual object per exit and join, with a linear bind search per virtual reference (lu_fs_vobj) | share caller frame-state prefixes between exits so a walk costs the innermost frame; index the binds; compile off the critical path (background thread) | 300 LOC sharing; 800 LOC background compile; recompile-churn policy ~50 |
| 5 | calls and frames | 1.15 (10%) | 10 / 4 | compiled-to-compiled calls go through h_call -> lu_eval -> lu_jit_run (async_tree: 36M generic compiled calls vs 3.1M direct per run); special methods through lu_wrapper_descr_call/kernel_wrapper/call_prepended/h_call_ex (27% inclusive in mdp) | direct unit-to-unit calls guarded on the function version; a wrapper descriptor's kernel target called directly | ~400 LOC |
| 6 | dict, attribute and type lookup | 0.98 (8%) | 10 / 2 | type_lookup 2-3% in 9 rows from many callers (prim_type_lookup in compiled dispatch when the type is not a constant, attr_shortcut, isinstance), probe_keys | type guard from site feedback, then fold the lookup (eval_prims already folds when the class is constant) | ~100 LOC |
| 7 | interpreter dispatch and profiling | 0.72 (6%) | 4 / 4 | 2to3 and cli.pygments start a fresh process per value (cold: compile 25-29%, interpreter 14-19%); django escape_filter stays interpreted (10.8%) between retire cycles | 4's churn fix; startup work (luc-uqiy) for the cold rows | - |
| 8 | other helpers, libc, C-API, locks, deopt | 0.89 (7%) | - | libc (memmove, memmem) 3.3%, C-API lu_call 1.6%, critical sections 1.4%, deopt/exits 0.3% (exits are cheap; recompiles are not) | - | - |

Not attempted in this lane: none of 1, 2, 4 or 5 is a small clearly bounded stage (each needs its own design and multi-row A/B), and the one small general item found (UTF-8 ASCII fast path) moves a single row. Suggested order: 4a (frame-state prefix sharing: compile time is 11% of steady time and dominates cold processes), then 2 (varargs forwarding), then 1 (kernel builtin customization), then 5.
