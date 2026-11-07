---
id: luc-15mf
status: open
deps: []
links: []
created: 2026-11-07T08:39:20Z
type: task
priority: 2
---
# JIT: keep values in registers across releases

DECREF is IF_CALL, so every value live across a release is spilled for its whole interval; in a loop that releases a box per iteration (AWFY Mandelbrot) the loop's floats live in memory. Its fast path needs only scratch registers, so it need not be a call if its slow path keeps the caller-saved registers that hold a value. A prototype during luc-cmel recorded each register's intervals in lu_regalloc and saved only those spanning the instruction at the slow paths (DECREF, BOX, UNBOX_INT's wide path, INLINE_LOAD's unboxed path): tests/kernel/re_corpus then crashed in the default mode even with DECREF still IF_CALL, so some value lives in a volatile register outside its recorded interval at UNBOX_INT's or INLINE_LOAD's slow path. Find it first, then drop IF_CALL from DECREF and measure.

