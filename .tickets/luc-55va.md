---
id: luc-55va
status: open
deps: []
links: []
created: 2026-11-08T16:14:44Z
type: bug
priority: 2
tags: [threads]
---
# type_set_mro releases the old MRO under lock-free MRO walks

prim_type_set_mro replaces tp_mro with Py_XSETREF, releasing the old tuple at once, while type_lookup's walk (lookup_in_mro) reads tp_mro without a lock. A thread assigning __bases__ can free the tuple another thread is iterating. CPython walks under TYPE_LOCK on a cache miss; here the walk is lock-free, so the old MRO wants a QSBR-delayed release (as replaced keys tables get) or the walk a reference. Found while fixing luc-m4w6; unreproduced.

