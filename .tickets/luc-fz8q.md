---
id: luc-fz8q
status: closed
deps: []
links: []
created: 2026-11-17T10:50:20Z
type: task
priority: 2
---
# close a suspended generator with no handler without throwing GeneratorExit

CPython gen_close clears a generator suspended at a RESUME with RESUME_OPARG_DEPTH1_MASK instead of throwing GeneratorExit; the kernel _close threw into every one (interpreted throw + handler + error exit per unfinished generator dropped: any(), next() early stops).

