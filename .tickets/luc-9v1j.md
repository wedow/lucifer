---
id: luc-9v1j
status: open
deps: []
links: []
created: 2026-11-04T16:59:24Z
type: bug
priority: 1
---
# Concurrent class creation corrupts the base's subclass list: hang or crash at exit

add_subclass, remove_subclass, lu_type_modified and the type_subclasses primitive (runtime/prim/type.c) read and change a base's tp_subclasses list with no lock. Eight threads that each make and read 400 short-lived classes (type('K', (), {'x': r}).x) hang at exit in about a quarter of runs on master and sometimes crash in type_dealloc -> subclass_of during Py_FinalizeEx. Lock the base around add/remove, and iterate a snapshot taken under the lock (lu_type_modified recurses and calls watchers, so it must not hold it).

