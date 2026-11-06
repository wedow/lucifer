---
id: luc-ucai
status: open
deps: []
links: []
created: 2026-11-06T21:49:51Z
type: task
priority: 3
assignee: Greg Wedow
---
# JIT: compile LOAD_SUPER_ATTR, FORMAT_SIMPLE, RAISE_VARARGS, module-level loops; recompile on hot side exit

Leftovers from luc-jp30. Module code is not marked optimized so module-level loops never compile (292 vs 196 ns/iter CPython). Pruned-branch flip recovers only after 50 guard failures; guard exits are small today (Flask ~2.9k/run) so low priority.

