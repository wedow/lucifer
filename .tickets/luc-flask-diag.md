---
id: luc-flask-diag
status: closed
deps: []
links: []
created: 2026-11-26T14:01:22Z
type: bug
priority: 2
---
# generalize attribute field loads that keep leaving compiled code

A compiled slot or inline load can leave on every read of a value owned by another thread. Rebuilding from the receiver shape repeats the failing field load.
