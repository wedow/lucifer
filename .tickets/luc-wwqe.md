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


## Notes

**2026-11-05T14:01:22Z**

After luc-34ha (port functions no longer bind), test_typing's 178 rows get past the mypy option parser's add() binding error and stop at a C-API type-creation bug: the MRO of mypyc's CapturableArgumentParser(argparse.ArgumentParser) is (CapturableArgumentParser, ArgumentParser, _AttributeHolder, object), missing _ActionsContainer, so super(ArgumentParser, self).__init__ reaches object.__init__ (TypeError: object.__init__() takes exactly one argument). CPython: (..., _AttributeHolder, _ActionsContainer, object). Repro: import mypy.main from the test venv and call CapturableArgumentParser(prog='mypy', add_help=False). Same on master.
