---
id: luc-3rna
status: closed
deps: []
links: []
created: 2026-11-07T21:09:55Z
type: task
priority: 2
tags: [jit, perf]
---
# JIT: virtual objects across a loop's back edge (virtual phis)

luc-k5az makes a tuple or instance virtual only when no phi takes it. A local assigned in a loop body (for ...: v = Vec(...)) keeps the previous iteration's object alive across the back edge, so the header's phi of that local is a use and the object stays real; so does an inlined callee's value returned from more than one place. Extend scalar replacement to phis whose inputs are virtual objects of one class (or unbound): the fields become phis, with a presence bit for an unbound input, and an exit makes the phi's object from them. LUCIFER_JIT_DEBUG prints 'an argument (PHI ...)' for each object this keeps real; on AWFY CD (_RedBlackTree._tree_insert, _Vector3D.plus), Havlak (find_loops) and DeltaBlue (projection_test) these are among the most frequent reasons.


## Notes

**2026-11-08T09:03:51Z**

Done for objects whose fields are their arguments (luc-esc2 6a3d1f7): a phi whose inputs are all loop iterators, or tuples of one size, is virtual with them; its fields are phis of their fields, which take the references the inputs held (phi_shapes, phis_agree, phi_fields in ssa_opt.c 3c). A tuple carried around a loop (a, b = t; t = (b, a + b)) is never made. Instances are left to luc-n6ke: the instance phis still reported on AWFY are of objects that escape anyway.
