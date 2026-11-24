---
id: luc-t4mn
status: open
deps: []
links: []
created: 2026-11-24T02:15:26Z
type: bug
priority: 3
tags: [capi]
---
# capi: a spec type with a managed dict and no HAVE_GC

CPython refuses Py_TPFLAGS_MANAGED_DICT (and MANAGED_WEAKREF) without Py_TPFLAGS_HAVE_GC in PyType_FromSpec (SystemError: the managed dict must be traversed). Lucifer accepts it. The compiled instance release (luc-eud7) now requires HAVE_GC for its plain-release bit, so such a type is released through the generic path and nothing is wrong today; refusing it as CPython does would keep extensions from depending on a combination CPython rejects. Decide whether to refuse it.

