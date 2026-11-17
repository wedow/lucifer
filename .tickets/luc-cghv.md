---
id: luc-cghv
status: closed
deps: []
links: []
created: 2026-11-17T08:39:00Z
type: task
priority: 2
---
# jit: compile property loads (LOAD_ATTR_PROPERTY)

Compiled code loaded a property through h_load_attr -> PyObject_GetAttr -> kernel generic_getattr -> property.__get__; the interpreter specializes LOAD_ATTR_PROPERTY. Call the getter (in line where it can), guarded on the class version and the property's fget. io open+read 280k -> 156k instructions/op.

