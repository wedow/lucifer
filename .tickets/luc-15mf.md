---
id: luc-15mf
status: closed
deps: []
links: []
created: 2026-11-07T08:39:20Z
type: task
priority: 2
---
# JIT: keep values in registers across releases

DECREF is IF_CALL, so every value live across a release is spilled for its whole interval; in a loop that releases a box per iteration (AWFY Mandelbrot) the loop's floats live in memory. Its fast path needs only scratch registers, so it need not be a call if its slow path keeps the caller-saved registers that hold a value. A prototype during luc-cmel recorded each register's intervals in lu_regalloc and saved only those spanning the instruction at the slow paths (DECREF, BOX, UNBOX_INT's wide path, INLINE_LOAD's unboxed path): tests/kernel/re_corpus then crashed in the default mode even with DECREF still IF_CALL, so some value lives in a volatile register outside its recorded interval at UNBOX_INT's or INLINE_LOAD's slow path. Find it first, then drop IF_CALL from DECREF and measure.


## Notes

**2026-11-08T23:57:07Z**

Done in luc-hez4 without the per-register intervals: a release that publishes nothing is not a call to the allocator (lu_alloc_call) and its slow path saves the caller-saved registers the unit allocates anywhere (a superset of what is live, so no interval reasoning); values live across real calls take rbx/r12/r13, which every call preserves, unless a call publishes them or the instruction's own code uses those registers (clobbers_saved). AWFY instructions: Richards -12%, DeltaBlue -5%, Havlak -4%, none up; Mandelbrot unchanged (its floats cross publishing releases or need XMM, which no call preserves).
