---
id: luc-5b0q
status: closed
deps: []
links: []
created: 2026-11-18T04:39:27Z
type: task
priority: 2
---
# jit: a method load tests the instance's dict in line

Gap report 2026-11-18 mechanism #1. LOAD_ATTR_METHOD_WITH_VALUES and NONDESCRIPTOR_WITH_VALUES call h_dict_lacks at every load (Richards 13 M calls per iteration). An instance with no dict of its own lacks the name: test the managed dict word in line and call only when there is one. Guarding it to be NULL instead was tried: DeltaBlue's instances with dicts made it leave 1.4 M times and grow 25% in instructions.


## Result

Landed: emit.c tests the managed dict word before the call and answers 1 in line when it is NULL. dict_lacks loads
per 3 AWFY iterations (all of which now skip the call when the instance has no dict): Richards 127 M, Towers 59 M,
Havlak 55 M, Queens 36 M, Permute 56 M, Storage 29 M, Bounce 23 M, CD 28 M, DeltaBlue 24 M. Steady instructions per
iteration: Towers 0.984, Queens 0.984, Bounce 0.985, Permute 0.992, Richards 0.992; AWFY cycles geomean 0.985 for the
pass (noise on this host is larger than the pass).
