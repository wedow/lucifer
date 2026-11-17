---
id: luc-gcwr
status: closed
deps: []
links: []
created: 2026-11-17T10:50:20Z
type: task
priority: 3
---
# dict(iterable) tests for keys without raising AttributeError

kernel has_keys used try: obj.keys except AttributeError, an error exit per dict(genexpr) in compiled code; CPython uses PyObject_GetOptionalAttr (lookup_attr).

