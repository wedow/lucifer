---
id: luc-m0na
status: closed
deps: []
links: []
created: 2026-11-03T09:49:14Z
type: task
priority: 1
tags: [runtime, kernel, opus]
---
# Inline caches: deferred refcount or QSBR before two tasks run at once

Specialized LOAD_ATTR caches (LOAD_ATTR_CLASS, _METHOD_*, _NONDESCRIPTOR_*) keep a borrowed descriptor pointer that only the type dict's reference and the type version guard protect. K1r's handoff runs one task at a time, so no carrier is between a guard and its use while another changes the type; tasks running at once (M1.8, threads) are not safe. cp314t specializes only when the cached object has a deferred refcount (Python/specialize.c, _PyObject_HasDeferredRefcount), so it is freed only by a stopped-world collection. Do the same when deferred refcounts land, or reclaim a replaced type attribute through QSBR (which delays weakref clearing, unlike cp314t's functions). Add a test with two tasks that mutate a class while another loops over a cached attribute.


## Notes

**2026-11-05T03:11:27Z**

Inline caches name only immortal or deferred objects (cp314t's rule); top-level functions, methods, descriptors and functions assigned to a class are deferred; a collection that frees a deferred object first clears every cache entry naming it, with the world stopped, so readers take a plain incref. threads_cache_churn, threads_slot_race and threads_dict_unpack_race crash on master and pass in plain, handoff, handoff:3, gc:100000, jit and jit-deopt. Heap types and modules are not deferred yet (cp314t defers both), so a class-valued class attribute no longer specializes.
