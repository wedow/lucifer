---
id: luc-rdfb
status: closed
deps: []
links: []
created: 2026-11-18T03:49:39Z
type: task
priority: 1
---
# jit: a call polls only where code may have run since the last poll

Gap report 2026-11-18 mechanism #1 (polls). Every inlined call polls at its RESUME; Richards runs 37 M polls per iteration. A call's poll is redundant where every path polled since the last instruction that published its state (ran code or parked).


## Result

Landed. facts.c call_polls drops a POLL_CALL where every path polled since the last publishing instruction; a parked
slow path (INLINE_LOAD, LIST_ITEM) now leaves when the poll word changed during the park (emit.c parked_end), so it
counts as a poll. Executed polls, 3 iterations, before -> after: Richards 170 M -> 133 M, Havlak 97 M -> 74 M, CD
56 M -> 41 M, NBody 40 M -> 25 M, Towers 49 M -> 35 M, DeltaBlue 17 M -> 14 M, Json 25 M -> 22 M. A poll is three
instructions, so steady instructions move under 1%; Richards cycles C vs D under perf stat, twice each: 128 G, 131 G
vs 125 G, 122 G per 10 iterations.
