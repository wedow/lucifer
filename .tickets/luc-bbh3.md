---
id: luc-bbh3
status: open
deps: []
links: []
created: 2026-11-07T11:15:33Z
type: bug
priority: 2
tags: [jit]
---
# JIT: an exit after a call can read a register the call clobbered

regalloc.c counts an interval as crossing a call only when the call is strictly inside it (calls[c] > start && calls[c] < end), and an instruction's frame-state uses sit at its own position. So a value whose last use is an IF_CALL instruction's frame state (a stack operand the instruction consumes) may get a register: a caller-saved one the C call clobbers, or RBX/R12/R13, which INLINE_STORE and INLINE_STORE_F/_I write before they can still decline. That instruction's exit after the call (a declined store, an error) then rebuilds the frame from the clobbered register. It is rare because the GUARD_FRAMES after most calls names the same locals, so they cross; no program reproduced it on luc-inline (two attempts: borrowed parameters, raw int locals). A fix tried there and dropped for want of a failing test: count an IF_CALL instruction's frame-state uses at pos + 1 (it subsumes the lu_publishes case). Cost: those stack operands spill.

