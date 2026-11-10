---
id: luc-ar8c
status: open
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

