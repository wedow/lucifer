---
id: luc-621k
status: closed
deps: []
links: []
created: 2026-11-11T14:14:47Z
type: bug
priority: 2
---
# lu_instr_before scans code from its start on every traceback, unwind and f_lineno

lu_instr_before (runtime/interp/frame.h) walks a code object from its first unit to find the instruction before a pc. It runs for every traceback entry, exception unwind, f_lineno read, deopt and interpreted PIC note (ceval INSTR_START). Measured share of scored cycles (docs/design/corpus-gap-2026-11-11.md): coverage 21%, cli.black 11%, sympy 8%, async_tree_eager 7%, coroutines 6%; corpus geomean -1.1% if it were free. Make it O(1): keep the instruction start, or a per-code start map.


## Notes

**2026-11-11** Fixed: each code object carries one bit per code unit marking
instruction starts, written once in lu_code_new after the bytecode; lookup is
the highest set bit at or below the unit before `after`. Daily-shaped cycles
per loop, best of 2, before → after: coverage 21.8G → 14.3G (−34%), cli.black
(whole run) 10.2G → 8.87G (−13%), sympy 18.5G → 17.1G (−7.5%),
async_tree_eager 2.97G → 2.71G (−9%), coroutines 141M → 141M (no change).
tools/timecheck `unwind` (20,000 raises at the end of a 20,000-line function)
went from over 2 s to 0.39 s (oracle 0.20 s).
