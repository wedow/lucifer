---
id: luc-s2pu
status: open
deps: []
links: []
created: 2026-11-04T09:32:04Z
type: bug
priority: 2
assignee: Greg Wedow
---
# FunctionType() constructor missing

types.FunctionType(code, globals, ...) is not callable; pdb and _pyrepl use it, so doctest and test_weakref cannot import. Match CPython's func_new (Objects/funcobject.c) including closure and kwdefaults checks. Found in luc-c702.

