---
id: luc-621k
status: open
deps: []
links: []
created: 2026-11-11T14:14:47Z
type: bug
priority: 2
---
# lu_instr_before scans code from its start on every traceback, unwind and f_lineno

lu_instr_before (runtime/interp/frame.h) walks a code object from its first unit to find the instruction before a pc. It runs for every traceback entry, exception unwind, f_lineno read, deopt and interpreted PIC note (ceval INSTR_START). Measured share of scored cycles (docs/design/corpus-gap-2026-11-11.md): coverage 21%, cli.black 11%, sympy 8%, async_tree_eager 7%, coroutines 6%; corpus geomean -1.1% if it were free. Make it O(1): keep the instruction start, or a per-code start map.

