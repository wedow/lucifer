---
id: luc-mj6t
status: open
deps: []
links: []
created: 2026-11-25T13:32:50Z
type: bug
priority: 2
---
# a frame object kept past its return reports the line of its last sync, not its return

Found by tests/kernel/entry_tracebacks.py (luc-o80b stage 0): a frame taken with sys._getframe() and read after the function returned has f_lineno at the _getframe line; CPython reports the return line. Fails in the plain, jit-off and jit lanes, so the interpreter is wrong too; passes in jit-deopt.

