---
id: luc-po0x
status: open
deps: []
links: []
created: 2026-11-08T19:44:46Z
type: task
priority: 2
tags: [jit, perf]
---
# jit: slot stores in compiled code take the object's lock in line

From luc-ci4f. h_slot_store calls lu_member_exchange, a full PyCriticalSection_Begin/End per store of a __slots__ member: 7.4% of cli.pygments' samples (the _sre _State object's slots). CPython 3.14t's STORE_ATTR_SLOT takes the owner's ob_mutex with one fast compare-and-swap (LOCK_OBJECT) and deopts when it is held. Compiled code can do the same in line and call C only when the lock is contended. Overlaps the attribute lane (luc-attr): coordinate.

