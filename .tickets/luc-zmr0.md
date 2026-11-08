---
id: luc-zmr0
status: in_progress
deps: []
links: []
created: 2026-11-08T18:17:46Z
type: task
priority: 1
tags: [perf, runtime]
---
# runtime-internal callers use inline versions of hot C-API shims

PyType_IsSubtype, PyVectorcall_Function, PyThreadState_GetUnchecked, PyInterpreterState_Get and _Py_Dealloc are 7.4% of corpus time, called as functions from the runtime's own hot paths. Give runtime callers inline internal versions; the exported ABI stays unchanged. Census 2026-11-08 mechanism 4.


## Notes

**2026-11-08T19:52:12Z**

Landed on luc-hotc: lucifer.h holds inline bodies of PyType_IsSubtype, PyObject_TypeCheck, PyVectorcall_Function, PyThreadState_Get/GetUnchecked, PyInterpreterState_Get and PyErr_Occurred and maps the runtime's uses to them; the exported functions wrap the same bodies (abi-audit unchanged). field_offset (field_get/field_set) tests exact types before MRO walks: it called PyType_IsSubtype up to 13 times per call, 13% of spectral_norm. _Py_Dealloc left as a function: its samples are the tp_dealloc load and dispatch, which inlining moves rather than removes. Instructions (fixed loops, before -> after): spectral_norm -17.6%, async_generators -7.4%, json_dumps -3.0%, richards -2.7%, deltablue -2.5%, raytrace -2.2%.
