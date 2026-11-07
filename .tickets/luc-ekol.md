---
id: luc-ekol
status: open
deps: []
links: []
created: 2026-11-07T16:00:23Z
type: feature
priority: 2
tags: [jit, perf]
---
# jit: closures made in inlined frames (MAKE_FUNCTION, MAKE_CELL)

Found by luc-keyu's census. MAKE_FUNCTION and MAKE_CELL compile only in the root frame; an inlined callee that makes a closure side-exits and is marked noinline (DeltaBlue _Plan.execute's for_each(lambda), _AbstractConstraint.satisfy and inputs_known with cells). luc-keyu inlines a closure the unit made and handed on by its code (made_function), but only when the root made it. Making functions in inlined frames (the helper with the frames pushed, or the function made virtual when it does not escape) would let methods like execute inline whole: the Smalltalk VM's block inlining gave Havlak -23%, DeltaBlue -20% (roadmap row 7).

