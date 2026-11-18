---
id: luc-yzbg
status: open
deps: []
links: []
created: 2026-11-18T20:49:46Z
type: feature
priority: 1
tags: [jit, perf]
---
# jit: a call that takes a tuple apart gets its items

Gap report 2026-11-18 mechanism #4, step 2. A not-inlined callee that returns a tuple it made, its caller taking it apart or returning it (UNPACK_SEQUENCE n or RETURN_VALUE next), made the tuple and boxed its numbers: json_loads's scanner returns (value, index) from _scanstring, _scan_once, _parse_object and _match_number, 2.7 M tuples and most of its 15 M ints per run. A root return of a tuple the code made (2 to 4 items) forks on the caller's ask (WANTS_TUPLE, bit 1 of the result word): RETURN_TUPLE gives the items, numbers raw and objects with a reference each, in the caller's result area; any other caller gets the tuple. The unit's shape at every return is published with its direct entry; a call of a known callee with such a shape asks for it and takes the items (CALL_ITEM: from the area, or from a tuple the callee gave), so the caller's state holds a tuple that does not exist. A tuple of another shape is made from its items (lu_jit_tuple_of). The direct call's result now goes to a result area in the caller's frame (RES_AREA), not over its callable's word.

## Acceptance Criteria

jitcheck lane tuples flat in tuples and int boxes; json_loads allocations down; gates pass.


## Notes

2026-11-18, not landed. Built and gated on top of luc-c31r. It does not fire on json_loads: _scan_once's
returns are of other calls' tuples (pass-through), so no unit summary; the RETURN_VALUE case (a callee's
tuple returned by the caller) made json_loads worse (tuples 2.67 M -> 2.83 M, ints +275 k per run: the
caller makes the tuple its callee no longer did), and without it only chaos moves (-12 k tuples). AWFY
DeltaBlue and Json +0.9%. Needs a per-site shape (what this call returned) rather than a unit summary, and
pass-through returns handled, before it can pay.
