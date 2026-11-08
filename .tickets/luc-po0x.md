---
id: luc-po0x
status: closed
deps: []
links: [luc-j2vs, luc-uexr, luc-4yfw, luc-n6yj, luc-6ljp, luc-r1pk, luc-mltz, luc-m3wi, luc-xtmp, luc-9ags, luc-o8i6]
created: 2026-11-08T19:44:46Z
type: task
priority: 2
tags: [jit, perf]
---
# jit: slot stores in compiled code take the object's lock in line

From luc-ci4f. h_slot_store calls lu_member_exchange, a full PyCriticalSection_Begin/End per store of a __slots__ member: 7.4% of cli.pygments' samples (the _sre _State object's slots). CPython 3.14t's STORE_ATTR_SLOT takes the owner's ob_mutex with one fast compare-and-swap (LOCK_OBJECT) and deopts when it is held. Compiled code can do the same in line and call C only when the lock is contended. Overlaps the attribute lane (luc-attr): coordinate.


## Notes

**2026-11-08T22:56:55Z**

luc-r1pk: lu_member_exchange (the C store behind compiled code's h_slot_store, the interpreter's STORE_ATTR_SLOT and PyMember_SetOne) takes the object's lock byte with one compare-and-swap, as CPython's LOCK_OBJECT does, and a critical section only when the byte is held. cli.pygments samples in the store path (critical section + exchange + helper): 10.1% -> 7.5%; PyCriticalSection_Begin/End 8.9% -> 1.4%. The remaining cost is the CAS and the unlock, which CPython pays too; a store in line without the helper call is what is left.
