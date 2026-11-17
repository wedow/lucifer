---
id: luc-gmvh
status: open
deps: []
links: []
created: 2026-11-17T18:47:24Z
type: bug
priority: 2
assignee: Greg Wedow
---
# list(d.values()) raises 'dictionary changed size during iteration' under concurrent mutation; CPython 3.14t does not

Seen while stress-testing the gc drain (5e65c7f). Pre-existing. CPython 3.14t builds the list under the dict's critical section; match that.

