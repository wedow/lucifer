---
id: luc-3rna
status: open
deps: []
links: []
created: 2026-11-07T21:09:55Z
type: task
priority: 2
tags: [jit, perf]
---
# JIT: virtual objects across a loop's back edge (virtual phis)

luc-k5az makes a tuple or instance virtual only when no phi takes it. A local assigned in a loop body (for ...: v = Vec(...)) keeps the previous iteration's object alive across the back edge, so the header's phi of that local is a use and the object stays real; so does an inlined callee's value returned from more than one place. Extend scalar replacement to phis whose inputs are virtual objects of one class (or unbound): the fields become phis, with a presence bit for an unbound input, and an exit makes the phi's object from them. LUCIFER_JIT_DEBUG prints 'an argument (PHI ...)' for each object this keeps real; on AWFY CD (_RedBlackTree._tree_insert, _Vector3D.plus), Havlak (find_loops) and DeltaBlue (projection_test) these are among the most frequent reasons.

