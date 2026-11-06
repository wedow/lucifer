---
id: luc-t0dg
status: open
deps: []
links: []
created: 2026-11-06T01:34:14Z
type: feature
priority: 3
---
# Tuples untracked at creation when no item can be tracked (3.14 maybe_tracked)

CPython 3.14 tracks a new tuple only when an item's type is GC (Objects/tupleobject.c maybe_tracked, in PyTuple_Pack, _PyTuple_FromArray and the stackref constructors), so gc.is_tracked(tuple([1, 2])) is False. lucifer tracks every tuple. test_capi.test_object EnableDeferredRefcountingTest.test_enable_deferred_refcount asserts it (and PyUnstable_Object_EnableDeferredRefcount then refuses the untracked tuple). Touches every tuple construction path, C and kernel, and the collector's view of tuples.

## Acceptance Criteria

gc.is_tracked(tuple([1, 2])) is False, gc.is_tracked(([],)) is True, matching the oracle; test_enable_deferred_refcount passes.

