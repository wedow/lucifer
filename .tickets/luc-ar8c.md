---
id: luc-ar8c
status: closed
deps: []
links: []
created: 2026-11-10T12:41:20Z
type: task
priority: 2
parent: luc-zelv
tags: [perf, jit]
---
# Compiled loops leave at every scheduled collection

OP_POLL leaves compiled code for every poll event but QSBR (and a kernel frame's visible reasons), so an allocating loop leaves at each LU_POLL_GC_SCHEDULED and the rest of that pass runs in the interpreter until a back-edge re-enters. Measured on luc-custom2: k_set_genexpr and k_tuple_genexpr take a poll exit at the root about once per call (2946 exits in 3000 calls), and ','.join(genexpr) leaves at the comprehension's back-edge on 438 of 2000 calls, after which the generator is resumed by its own unit instead of in line. Lever: service a scheduled collection in place at the poll, as an opaque call does (publish the root's state, push inlined frames in line), and leave only when it raised or something must change the unit (invalidation, stop, tools).


## Notes

**2026-11-10T16:33:28Z**

Done on luc-custom3: the poll's slow path makes a refcount merge (LU_POLL_EXPLICIT_MERGE, which QSBR retirement raises every few retired arrays, so any loop that grows lists, sets or dicts kept leaving) and, outside a builtin, a scheduled collection in place, with the frames published as for a call that may park (parked_call), then reads the word again; other events still leave. Poll exits on the 15 programs at 3x reference loops: nqueens 1814 -> 1, pickle 127, unpickle 162, go 89 -> about 1-11. tuple(genexpr) 1086 -> 940 instructions per item; the exits had cost little elsewhere (nqueens -0.3%).

**2026-11-10T19:22:03Z**

Side effect on mako (1.24x master wall): the base's poll exits had let the interpreter OSR-compile str.join's comprehension loop with the deque iterator's step inlined; without them the generic unit keeps the loop. Ticketed as luc-6scb (megamorphic FOR_ITER).
