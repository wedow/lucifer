---
id: luc-9drr
status: open
deps: []
links: []
created: 2026-11-11T14:14:47Z
type: bug
priority: 2
---
# coverage runs 1.75x slower with the JIT on than off

pyperformance.coverage: daily-shaped ratio to CPython 3.9x with -X lucifer_jit=off, 6.9x with the JIT on (docs/design/corpus-gap-2026-11-11.md). Under the tracer nothing compiles (compiled share 0%), yet the JIT-on path pays for instrumented-opcode lookups (original_opcode_at, lu_base_opcode, lu_instrumented_caches: 8%) and the lu_instr_before scan (21%). Find what the JIT-on path does per traced event that the JIT-off path does not.

