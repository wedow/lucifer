---
id: luc-wwqe
status: open
deps: []
links: []
created: 2026-11-04T17:48:46Z
type: bug
priority: 2
parent: luc-h4gc
---
# mypyc-compiled mypy aborts in a C-API stub while importing

numpy/typing/tests/test_typing.py (178 rows) runs mypy.api.run in-process. The mypy in the test environment is mypyc-compiled: it needs PyObject_GetItem on classes (fixed here), then a C-API function that is still a stub aborts during mypy.typeanal's module init (lu_unimplemented under _PyState_AddModule's neighbourhood; find the stub with the stderr message). Implement the C-API calls the mypyc runtime uses and check that mypy.api.run runs a program.

