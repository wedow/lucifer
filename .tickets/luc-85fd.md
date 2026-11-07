---
id: luc-85fd
status: open
deps: []
links: []
created: 2026-11-07T12:09:04Z
type: bug
priority: 2
assignee: Greg Wedow
---
# jitcheck inlined lane: call growth 1.19 per iteration once under load

luc-x8q2's first check-jit run failed the inlined lane (1.19 calls per iteration, 1 allowed). A deterministic re-measure gives 1.00 on master and branch. New spread is a timing-dependent optimizer until shown otherwise: find what makes the call count depend on timing (compile timing, invalidation at a poll, recompile) and make it deterministic or count it.


## 2026-11-07 second occurrence
luc-23yk's first check-jit run on the merged head failed the inlined lane again: 240,084 calls at 20,000 iterations, 263,973 at 40,000 (allowed 1 per iteration). By hand master and branch both grow by exactly 20,000; the rerun passed. Two occurrences on a busy host, so the count depends on timing.
