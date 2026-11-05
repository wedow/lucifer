---
id: luc-y0g5
status: closed
deps: []
links: []
created: 2026-11-04T23:53:08Z
type: bug
priority: 3
---
# Borrowed class values in the inline caches and unlocked lu_ns_next

Left by luc-ojw0. LOAD_ATTR_METHOD/CLASS and the translator now try-incref the cached descriptor; other specialized instructions and the JIT constants of cached pointers (LOAD_GLOBAL values, CALL targets, store caches) still read pointers the cache does not own. lu_ns_next iterates a dict without its lock (callers: update/kwargs paths on private dicts, type.c class creation); lu_ns_lookup remains for startup and private dicts.


## Notes

**2026-11-05T03:11:27Z**

Closed with luc-m0na: slot loads try-incref and slot stores take the member lock (interpreter and JIT helpers); the translator reads a site's whole specialization under the writer's protocol and takes descriptors and globals with references; lu_ns_next callers on shared dicts hold the dict's critical section. Cells remain: luc-lssm.
