---
id: luc-wdck
status: closed
deps: []
links: []
created: 2026-11-17T23:51:30Z
type: bug
priority: 1
---
# jit: dulwich_log segfaults after 10 warm-up values

pyperformance dulwich_log, run as the pyperformance-warm suite runs it (-w 10 -n 5 at the reference loops), dies with SIGSEGV on master e4cebea; the daily's one-warm-up shape passes.

