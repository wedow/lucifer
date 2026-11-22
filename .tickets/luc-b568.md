---
id: luc-b568
status: closed
deps: []
links: []
created: 2026-11-22T04:04:34Z
type: task
priority: 1
assignee: Greg Wedow
tags: [perf]
---
# compiled code calls int_mul and rangeiter_next prims out of line

luc-tax0 follow-up: after aabbc3e7 and 36ecfdd0, the next prim family by count from compiled code is async_tree's int_mul (18M per run) and rangeiter_next (18M). Lower them in compiled units the way aabbc3e7 lowered size and seq_get: inline the small-int multiply with an overflow check, falling back to the prim, and advance range iterators inline when the iterator's type is known. First count prim calls per primitive across rows (with a counting build) and take the top two families, in case they differ from these. General mechanism only.


## Notes

**2026-11-22T04:37:49Z**

A temporary counting build sampled generic primitive calls in 14 pyperformance rows. The survey used the 12 attribution rows from luc-tax0 and the eager and task-group variants of the mixed async tree. Counts subtract a worker with one measured value from a worker with two, both after one warmup at one loop. Totals were 197.62M for type_lookup (tracked in luc-szry), 18.14M for rangeiter_next, 18.03M for int_mul, 13.07M for str_cmp and 7.91M for dict_find. Across the original 12 rows, str_cmp led the primitives outside luc-szry at 11.73M.

Compiled calls reuse the existing range-step instructions for exact range iterators. A checked machine-word multiply alone gave up at sites with frequent overflow. Those sites now multiply compact operands inline and call the primitive for wider operands. In async_tree_cpu_io_mixed, rangeiter_next went from 6,029,202 calls to zero and int_mul went from 6,005,833 to 3,248,291 per additional value.

The intrange growth check went from 11 to 9 helper calls per added iteration. Its output and the eight neighboring programs matched CPython 3.14t in plain, jit and jit-deopt. The targeted jitcheck lanes and check-harness passed.

The table gives median user-space instructions over five alternating runs per arm, built and run at the same paths with PYTHONHASHSEED=0. The microbenchmark uses 2,000,000 iterations of the regression loop. The three pyperformance measurements include startup, one warmup and two measured values at one loop. The rows share the same factorial workload under different async scheduling configurations.

| Workload | Base | Branch | Change |
| --- | ---: | ---: | ---: |
| Range and multiply microbenchmark | 6,358,955,116 | 5,481,788,380 | -13.79% |
| async_tree_cpu_io_mixed | 104,753,110,376 | 100,540,547,462 | -4.02% |
| async_tree_eager_cpu_io_mixed | 98,176,303,648 | 94,107,042,889 | -4.14% |
| async_tree_cpu_io_mixed_tg | 103,366,782,580 | 99,265,688,724 | -3.97% |

**2026-11-22T05:29:16Z**

Review: correct at every multiply boundary (compact, 2^62/2^63, -1 * -2^63, bools, subclasses, NotImplemented) and range case (negative and empty steps, word and long iterators, exhaustion, length_hint, shared iterators across threads); jit_int_range_edges checks them in plain, jit and jit-deopt. Whole-run minimum of 7 at n=4, base vs branch on rebased master: async_tree_cpu_io_mixed cycles -2.4%, instructions -4.1%; nbody -3.5%/-2.1%; spectral_norm -3.3%/-3.6%; fannkuch -2.4%/-0.6%; chaos -1.7%/-4.1%; raytrace -0.0%/-1.2%; deltablue -2.4%/-3.0%; mdp -3.0%/-0.1%. Compiles drop by 8-11 per row.
