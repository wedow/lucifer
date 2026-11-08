---
id: luc-qpu2
status: open
deps: []
links: []
created: 2026-11-08T03:57:41Z
type: task
priority: 3
tags: [jit, perf]
---
# jit: compile module-level loops

Split from luc-ucai. Module code is not CO_OPTIMIZED, so compile_at never compiles it and a loop at module level runs interpreted (292 vs 196 ns/iter CPython in luc-jp30). Needs LOAD_NAME/STORE_NAME over the module dict (generic helpers like the new H_load_global/H_store_global) and an OSR entry at the loop header. One-shot module code must not compile: the size-scaled back-edge threshold already guards that.


## Notes

**2026-11-08T19:44:38Z**

luc-ci4f census (27 programs: async_tree family, regex, cli, generators): no module-level code object is among the top 40 interpreted codes of any of them; interpreted time is regex matchers (_entry, _match) and generators. Not a priority for this corpus.
