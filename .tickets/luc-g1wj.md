---
id: luc-g1wj
status: closed
deps: []
links: []
created: 2026-11-03T02:15:02Z
type: bug
priority: 2
tags: [runtime]
---
# Initialize mimalloc from .preinit_array so allocations in extension constructors on worker threads cannot share the main heap

## Acceptance Criteria

a capi test that first allocates on worker threads from a preloaded extension constructor passes 15/15; today it fails 11 of 15


## Notes

**2026-11-03T02:27:59Z**

Fixed: mimalloc starts from a .preinit_array entry in runtime/object/alloc.c, which ld.so runs before any constructor, so a preloaded extension's worker threads no longer take the first thread for the main one. Test tests/capi/preload_threads.c (8 threads, barrier-started, cross-thread frees, pattern-checked, 3 rounds, from an extension constructor). It failed 5 of 15 runs here before the fix (the ticket saw 11) and passes 150 of 150 after. make check-capi now runs every test 15 times.
