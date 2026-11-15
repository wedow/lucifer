---
id: luc-ax3p
status: open
deps: []
links: []
created: 2026-11-15T11:23:26Z
type: bug
priority: 2
assignee: Greg Wedow
---
# signals_deep_recursion fails intermittently in plain and jit kernel lanes

tests/kernel/signals_deep_recursion.py fails intermittently in make check under host load: plain lane (11-15, perf-gate2 at c5901b7) and jit lane (several gate runs 11-14/15). An earlier report said the CPython oracle timed out. Find out whether the oracle or lucifer is slow under load or the test has a timing assumption (cf. luc-fpg6, fixed by making the test cheaper), and fix the test or the runtime accordingly.

