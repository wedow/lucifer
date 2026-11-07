---
id: luc-1gqu
status: open
deps: []
links: []
created: 2026-11-07T21:09:55Z
type: task
priority: 2
tags: [jit, perf]
---
# JIT: virtual closures for callbacks to inlined callees

Census of compiled code's allocations per AWFY iteration (-X lucifer_jit_stats, luc-k5az): Havlak makes 2.54M functions and as many cell tuples (Vector.for_each(lambda ...), IdentitySet.contains, Set.contains, _UnionFindNode.find_set), DeltaBlue 0.28M (_Planner._add_constraints_consuming_to and others). MAKE_FUNCTION is a root-only helper and the closure's cell tuple escapes into it. When the callee that takes the function inlines and calls it with the function's identity known, the function and its cells need not exist: inline the call, keep the cells as values, and make them at exits. The Smalltalk VM measured Havlak -23% and DeltaBlue -20% from block identity through arguments (jit-roadmap section 1, row 7).

