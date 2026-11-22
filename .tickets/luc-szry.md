---
id: luc-szry
status: closed
deps: []
links: []
created: 2026-11-21T16:42:43Z
type: task
priority: 1
assignee: Greg Wedow
tags: [perf]
---
# compiled dispatch calls type_lookup when the type is not a constant

luc-tax0 row 6: type_lookup is 2-3% of 9 of 12 rows, called from prim_type_lookup in compiled binary_op/compare dispatch, attr_shortcut and isinstance when the receiver type is not a compile-time constant. eval_prims already folds the lookup when the class is constant. Use the site's type feedback to guard on the observed type (exit on mismatch) and then fold the lookup to a constant under the type's version tag. No program keys. Measure async_tree, mdp, deltablue and a microbenchmark.


## Notes

**2026-11-21T23:46:26Z**

Review (branch rebased on master, conflicts with the dropped-profile learning resolved: lu_jit_prof_learns and lu_jit_prof_kinds read value_kind): no defect found. The folded lookup stays correct against CPython in plain, jit, jit-deopt (7 and 3) and handoff when, mid-loop, the root base's attribute is set or deleted, an intermediate base shadows and unshadows, the class itself sets and deletes, a miss is filled on the root base, B.__bases__ is reassigned both ways, and a metaclass mro() reorders Q.__mro__ on __bases__ assignment; four threads spinning on a folded lookup all see a base's final value after 2,000 writes and __bases__ reassignments. Kernel jit_* plus 14 class and attribute neighbours pass in plain/jit/jit-deopt; all jitcheck growth lanes pass, typelookup 73,049 at both sizes. A/B deferred (host quiet window); branch not landed.

**2026-11-22T04:45:42Z**

Landed after rebase on master. Targeted re-gate: kernel jit_* plus 14 class and attribute neighbours pass in plain, jit and jit-deopt (jit_reclaim_fork, the known flake, failed once in jit); all jitcheck growth lanes pass, typelookup 73,049 at both sizes. A/B at one checkout path, whole run n=4 at loops/4, minimum of 7 alternating runs on one core, PYTHONHASHSEED=0: async_tree_cpu_io_mixed cycles 90.85G -> 86.27G (-5.0%), instructions 169.90G -> 169.97G (+0.0%); mdp 166.27G -> 161.00G (-3.2%), 277.20G -> 277.38G (+0.1%); deltablue 3.616G -> 3.567G (-1.4%), 5.541G -> 5.528G (-0.2%); docutils 251.71G -> 242.41G (-3.7%), 302.90G -> 290.24G (-4.2%). Compiles 690 -> 696, 689 -> 689, 534 -> 535, 3082 -> 3157.
