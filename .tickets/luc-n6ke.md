---
id: luc-n6ke
status: open
deps: []
links: []
created: 2026-11-08T08:03:52Z
type: task
priority: 3
tags: [jit, perf]
---
# JIT: virtual phis of instances

From luc-3rna. luc-esc2 made phis virtual for objects whose fields are their arguments (loop iterators, tuples of one size: phi_shapes/phis_agree/phi_fields in ssa_opt.c 3c). Instances are not: their fields are stores by dominance, so a phi needs the same field set on every input edge and field phis by offset, with a presence bit where a local is unbound. Measured payoff is small for now: the instance phis LUCIFER_JIT_DEBUG reports on AWFY (Havlak find_loops@401/461, DeltaBlue projection_test@75/86, CD _Vector3D.plus@90, _put_into_map@27) are of objects that escape anyway (passed to calls that do not inline, stored), so do this after those callees inline.

