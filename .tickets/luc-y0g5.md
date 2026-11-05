---
id: luc-y0g5
status: open
deps: []
links: []
created: 2026-11-04T23:53:08Z
type: bug
priority: 3
---
# Borrowed class values in the inline caches and unlocked lu_ns_next

Left by luc-ojw0. LOAD_ATTR_METHOD/CLASS and the translator now try-incref the cached descriptor; other specialized instructions and the JIT constants of cached pointers (LOAD_GLOBAL values, CALL targets, store caches) still read pointers the cache does not own. lu_ns_next iterates a dict without its lock (callers: update/kwargs paths on private dicts, type.c class creation); lu_ns_lookup remains for startup and private dicts.

