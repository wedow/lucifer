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

