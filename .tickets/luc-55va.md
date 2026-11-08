---
id: luc-55va
status: closed
deps: []
links: []
created: 2026-11-08T16:14:44Z
type: bug
priority: 2
tags: [threads]
---
# type_set_mro releases the old MRO under lock-free MRO walks

prim_type_set_mro replaces tp_mro with Py_XSETREF, releasing the old tuple at once, while type_lookup's walk (lookup_in_mro) reads tp_mro without a lock. A thread assigning __bases__ can free the tuple another thread is iterating. CPython walks under TYPE_LOCK on a cache miss; here the walk is lock-free, so the old MRO wants a QSBR-delayed release (as replaced keys tables get) or the walk a reference. Found while fixing luc-m4w6; unreproduced.


## Notes

**2026-11-08T17:15:28Z**

Reproduced without any patch: a writer swapping C.__bases__ while readers look attributes up through C crashed in lookup_in_mro (lu_dict_find_native on a base read from a freed MRO tuple). The window needs a walk preempted midway, so it shows under load (3 readers: 26 of 72 runs at 12 at once, 0 serially) or with more readers than cores in one process (16 readers, C's version tags spent so every lookup walks: 20 of 20 serially in each of the plain and jit lanes); no sleep patch was needed. Fix: type_set_mro and type_set_bases release the replaced tuple after a QSBR grace period (_PyObject_XDecRefDelayed), and the walk takes a reference to the MRO right after loading it, since a dict lookup that waits for its lock detaches, which QSBR counts as quiescent. After: 0 of 200 serially (plain and jit), 0 of 48 at 6 at once, 0 of 200 of the 3-reader variant at 12 at once. Test: tests/kernel/threads_mro_churn.py (fails every run before, about 1 s).
