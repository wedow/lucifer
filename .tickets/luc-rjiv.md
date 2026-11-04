---
id: luc-rjiv
status: open
deps: []
links: []
created: 2026-11-04T04:33:39Z
type: bug
priority: 1
---
# Lock-free container reads need CPython's free-threaded protocol: try-incref plus QSBR-delayed frees for dict/list/set storage and values

Review of luc-njhp: there is no try-incref and no deferred freeing, so values read lock-free from dicts and slots (prim_dict_value, the native fast paths) can be freed under the reader. Also: lu_set_resize writes mask before table and publishes the table before filling it (set probe tear). Port _Py_TryIncrefCompare and the QSBR-retired keys/items arrays from free-threaded dictobject.c/listobject.c/setobject.c. Prove with threaded kernel programs under handoff lanes.

