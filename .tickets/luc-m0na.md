---
id: luc-m0na
status: open
deps: []
links: []
created: 2026-11-03T09:49:14Z
type: task
priority: 1
tags: [runtime, kernel, opus]
---
# Inline caches: deferred refcount or QSBR before two tasks run at once

Specialized LOAD_ATTR caches (LOAD_ATTR_CLASS, _METHOD_*, _NONDESCRIPTOR_*) keep a borrowed descriptor pointer that only the type dict's reference and the type version guard protect. K1r's handoff runs one task at a time, so no carrier is between a guard and its use while another changes the type; tasks running at once (M1.8, threads) are not safe. cp314t specializes only when the cached object has a deferred refcount (Python/specialize.c, _PyObject_HasDeferredRefcount), so it is freed only by a stopped-world collection. Do the same when deferred refcounts land, or reclaim a replaced type attribute through QSBR (which delays weakref clearing, unlike cp314t's functions). Add a test with two tasks that mutate a class while another loops over a cached attribute.

