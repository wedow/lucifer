---
id: luc-80yg
status: closed
deps: []
links: []
created: 2026-11-17T08:39:00Z
type: task
priority: 2
---
# gc: forget only in code that cached a descriptor

A collection whose garbage holds a deferred object made every code object scan its whole bytecode (lu_specialize_forget), about half of GC time in import_stdlib. Mark code when an attribute site caches a descriptor; pass over the rest.

