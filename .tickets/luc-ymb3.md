---
id: luc-ymb3
status: open
deps: []
links: []
created: 2026-11-03T11:39:56Z
type: bug
priority: 1
tags: [runtime, mn, design]
---
# Under migration, _PyObject_IsUniquelyReferenced is false for objects the task allocated on another carrier, so marshal.dumps (and pyc writing) sets FLAG_REF differently from a single-thread run

Found merging K1r: marshal_dumps differs under the handoff lanes. CPython 3.14t marshal's w_ref skips refs only when _PyObject_IsUniquelyReferenced (owned by current thread, local==1, shared==0). Under M:N one Python thread crosses carriers, so runtime-internal uniqueness checks change observable output (pyc reproducibility). Decide: count ownership by task for runtime-internal checks, merge on migration, or pin while marshalling; check every other _PyObject_IsUniquelyReferenced/_Py_IsOwnedByCurrentThread user for the same issue.

## Acceptance Criteria

marshal_dumps matches the oracle under every handoff lane and is removed from HANDOFF_XFAIL in tools/kernelcheck; a list of the other uniqueness-check users and their status is in the ticket

