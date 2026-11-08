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


## Notes

**2026-11-08T08:03:41Z**

Branch luc-esc2, not landed. Tried: (1) a walker reads an inlined frame's locals only during a helper that may run Python (lent frames), the root's at any publishing instruction, so a closure in an inlined callee's local survives IF_SEEN stores; (2) MAKE_FUNCTION's function as a virtual object (fields: code and globals constants, closure via set_function_attribute flag 8; exits make it; only with no function watchers). On a reduced find_set it removed the function allocation, but on AWFY it removed none: Havlak's and DeltaBlue's closures are passed to calls that do not inline (find_set@110, each@186, Set.contains, IdentitySet.contains, _process_edges, _do_dfs: 'an argument (HELPER call)') or sit in a root local that a cell_load's published state names. The cell tuple would also need nested virtual objects (a virtual tuple as a virtual function's field). Payoff waits on inlining those callees (or the compiled call protocol) first.
