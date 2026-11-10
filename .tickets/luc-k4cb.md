---
id: luc-k4cb
status: open
deps: []
links: []
created: 2026-11-10T09:01:35Z
type: bug
priority: 2
assignee: Greg Wedow
---
# jit: unlink generator frames before their locals are released

luc-crash unlinks returning frames first (OP_UNLINK) for ordinary functions only; generator frames in compiled code still release locals while current. Add a finalizer-makes-frame test for a generator, as tests/kernel/frame_teardown.py does.

