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

