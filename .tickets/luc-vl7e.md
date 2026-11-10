---
id: luc-vl7e
status: open
deps: []
links: []
created: 2026-11-09T04:58:34Z
type: bug
priority: 1
---
# libtest: unimplemented C-API functions abort the worker and zero whole Lib/test modules

Each call to an unimplemented C-API function aborts the process, so one case takes down every case of its module. Seen in the full Lib/test run (2026-11-09): _PyTime_FromLong (test_time), _Py_normpath (test_fileutils), _PyMem_GetCurrentAllocatorName (test_sys), PyMarshal_ReadLastObjectFromFile (test_marshal), _PyInterpreterConfig_InitFromDict (test_import), _Py_Get_Getpath_CodeObject (test_getpath, at import), PyFunction_SetVectorcall (test_call), _PyCode_ReturnsOnlyNone (test_code), _Py_GetErrorHandler (test_codecs), _PyTime_ObjectToTime_t (test_time). Implement them, or raise SystemError instead of aborting so only the calling case fails. The crashing cases are listed in results/libtest/lucifer.excluded.tsv against this ticket; remove each row as its function lands.


## Notes

**2026-11-09T09:18:51Z**

Full run 2026-11-09: more aborting stubs: PyErr_DisplayException (test_traceback, 377 cases), _PyTraceMalloc_GetTraceback (test_tracemalloc 51), _Py_Get_Getpath_CodeObject (test_getpath 32, at import), _PyType_SetVersion (test_type_cache 7). 508 cases in all, counting the left-out ones.
