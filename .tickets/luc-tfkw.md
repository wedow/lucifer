---
id: luc-tfkw
status: open
deps: []
links: []
created: 2026-11-06T09:18:09Z
type: feature
priority: 3
---
# HAMT (Python/hamt.c) for _PyContext_NewHamtForTests and the contexts

The stdlib test_context's HamtTest cases call _testinternalcapi.hamt(), which calls _PyContext_NewHamtForTests. lucifer has no HAMT (its contexts copy dicts), so the hook raises NotImplementedError and those cases error instead of killing the worker. Porting Python/hamt.c (about 2900 lines) would give them a HAMT, and the contexts too (luc-exj1).

