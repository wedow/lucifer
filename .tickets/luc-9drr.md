---
id: luc-9drr
status: closed
deps: []
links: []
created: 2026-11-11T14:14:47Z
type: bug
priority: 2
---
# coverage runs 1.75x slower with the JIT on than off

pyperformance.coverage: daily-shaped ratio to CPython 3.9x with -X lucifer_jit=off, 6.9x with the JIT on (docs/design/corpus-gap-2026-11-11.md). Under the tracer nothing compiles (compiled share 0%), yet the JIT-on path pays for instrumented-opcode lookups (original_opcode_at, lu_base_opcode, lu_instrumented_caches: 8%) and the lu_instr_before scan (21%). Find what the JIT-on path does per traced event that the JIT-off path does not.


## Notes

**2026-11-11** Same cause as luc-621k. Under the tracer every generic
LOAD_ATTR/STORE_ATTR runs interpreted, and with the JIT on each one computes
INSTR_START() for lu_jit_pic_note_interpreted: the lu_instr_before scan, whose
lu_caches_at calls on INSTRUMENTED_LINE units are the original_opcode_at and
lu_base_opcode samples. The JIT-off path skips the note. With the luc-621k
start map, coverage cycles per loop (2→4 loops, best of 2): JIT on 22.3G →
14.5G; JIT off 12.7G → 13.6G. The on/off ratio fell from 1.75x to 1.07x.
The JIT-off move is not work: instructions per loop are 28.24G before and
28.26G after, and an unpinned rerun measured 14.91G cycles for both.
