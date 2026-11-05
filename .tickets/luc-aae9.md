---
id: luc-aae9
status: open
deps: []
links: []
created: 2026-11-05T02:14:19Z
type: bug
priority: 1
tags: [runtime, capi, numpy]
---
# segfault converting a list into a structured dtype with an object field when an int field fails

np.array([1, 'object'], [('x', int), ('y', object)]) segfaults; CPython raises ValueError: invalid literal for int() with base 10: 'object'. numpy/_core/tests/test_regression.py::TestRegression::test_mem_array_creation_invalid_specification scores CRASH. Reproduces with -X lucifer_jit=off, on master and on the luc-h4gc head before review, so neither the JIT nor that branch's review fixes cause it. np.array([1, 2], same dtype) works.

