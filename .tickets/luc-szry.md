---
id: luc-szry
status: open
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

