---
id: luc-b010
status: open
deps: []
links: []
created: 2026-11-03T09:29:21Z
type: task
priority: 3
tags: [capi]
---
# C-API: Py_GenericAlias is a stub; implement it over the kernel generic_alias function

runtime/capi/stubs.c still has Py_GenericAlias unimplemented; the kernel registers generic_alias (lib/kernel/genericalias.py make_alias) in the kernel function table, so the C function can call it like other thunks.

