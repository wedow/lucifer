---
id: luc-wtkb
status: open
deps: []
links: []
created: 2026-11-17T19:50:56Z
type: bug
priority: 1
assignee: Greg Wedow
---
# reading another thread's generator gi_frame.f_locals while a __del__ runs segfaults (JIT off)

Repro probe from the drain review: rv-scratch/py/xthread_frame.py (copy into the ticket's work dir). Segfaults in off mode on master and before dd66bc1; CPython 3.14t passes 3/3. Output varies run to run, so a kernel test needs a deterministic variant.

