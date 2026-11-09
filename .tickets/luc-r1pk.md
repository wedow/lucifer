---
id: luc-r1pk
status: closed
deps: []
links: [luc-j2vs, luc-uexr, luc-4yfw, luc-n6yj, luc-6ljp, luc-mltz, luc-m3wi, luc-xtmp, luc-9ags, luc-po0x, luc-o8i6]
created: 2026-11-08T18:09:49Z
type: epic
priority: 2
tags: [jit, perf, scaling]
---
# attribute lookup and object creation: census, then general fixes

Corpus census 2026-11-08 mechanisms 7 (attribute/type lookup, 6.9% of log-weighted time) and 8 (object creation through type.__call__, 5.6%). Census on Flask, AWFY Json/DeltaBlue/Havlak and attribute-heavy pyperformance programs, rank by time, fix in general form. Folds in luc-9ags, luc-n6yj, luc-4yfw, luc-xtmp, luc-m3wi, luc-uexr, luc-o8i6, luc-j2vs.


## Notes

**2026-11-08T22:10:28Z**

Census (perf + -X lucifer_jit_stats, master then branch) on Flask in process, AWFY Json/DeltaBlue/Havlak, raytrace, chaos, go, deltablue, richards, five async_tree variants, dulwich_log and the four sqlglot programs. Ranked: (1) AWFY Json's 51.6M refused calls of str.__getitem__ (binary_op's generic subscript); (2) go's Square instances moved to dicts (a name assigned out of index order, and a name only Board assigns with no room left), so the interpreter's caches thrashed to the generic LOAD_ATTR (4.5M generic loads per run, type_lookup 7.8%); (3) the generic path's repeated hook lookups (object_attr_hook twice per access); (4) kernel-new class calls (list, set, bytearray, exceptions) through type.__call__; (5) polymorphic stores and inlined frames' generic sites (Richards, DeltaBlue); (6) class attribute writes and uncached MRO walks (scaling). Fixes, one commit each: str subscript inlines str.__getitem__; instances keep an order record and CPython's room (30 names, usable count); classes keeping object's hooks get PyObject_GenericGetAttr/SetAttr in their slots; kernel-new class calls in line; store PICs and interpreter-fed PICs; lock-free MRO walk; native class attribute writes without the type lock for untagged types. Timing (exclusive bench host, best of 3 pyperf values; before merging luc-hotc's master): corpus subset geomean 0.922x master's time (master against itself: 1.000, 0.92-1.06), go 0.49x, richards 0.60x, richards_super 0.74x, deltablue 0.88x, sqlglot parse/transpile 0.93x/0.89x, async_tree family 0.93-1.06 (noise band); AWFY geomean 0.971x master (Json 0.744x: 1.17x CPython 3.14t from 1.56x, Havlak 0.93x, List 0.92x); Flask in process 0.99x. Leftovers: luc-mltz (keyword kernel class calls), luc-6ljp (interpreter-fed method PICs), luc-uexr, luc-o8i6 (raytrace's type_lookup is binary_op's operand dispatch), luc-j2vs.

**2026-11-08T22:36:22Z**

Also fixed on the way: the check after a generator's in-line entry (gen_enter) was an ordinary guard, so -X lucifer_stress=deopt:N left after the entry had made the generator running and the interpreter raised 'generator already executing' (master fails tests/kernel/jit_gen_enter_stress.py, an ast.walk-shaped loop, at every stress period; master's ast_module passed only by its guard pattern). And object keeps its attribute thunk so that extension types that set PyObject_GenericGetAttr themselves (ast nodes) keep their __getattribute__ wrapper, as in CPython.

**2026-11-08T23:49:44Z**

Re-timed after merging master (luc-hotc, luc-cold) against that master, same host and method: corpus subset geomean 0.946x (go 0.70x, richards 0.65x, richards_super 0.73x, deltablue 0.86x, sqlglot_v2 0.89x, parse 0.87x, transpile 0.86x; async_tree family 0.89-1.10, its noise band); AWFY geomean 0.976x (Json 0.783x, 1.16x CPython 3.14t; Havlak 0.95x, List 0.91x); Flask in process 1.00x; class writes 6.42/22.43/30.07 M/s at 1/8/32 threads (master 2.04/1.82/1.39, CPython 7.51/3.95/2.88); uncached lookups 43.1/154.1/402.1 M/s (master 33.8/120.8/165.7, CPython 25.4/87.4/159.6). Also folded in luc-po0x. Gates on the merged head: check, abi-audit, check-jit, check-harness, check-compiler all 0.
