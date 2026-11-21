---
id: luc-np6a
status: open
deps: []
links: []
created: 2026-11-21T09:52:03Z
type: task
priority: 1
assignee: Greg Wedow
---
# startup: name lookups take ~5 layers per probe for exact-str keys

luc-w2ra attribution (11-21): global_get and dict probes cost ~43M instructions of -S -c pass vs ~10M in CPython. Each module-level name load goes through about five layers per probe. Add a tight lookup path for exact-str keys with a cached hash (as CPython's unicode-keys dict lookup does), used by global/builtin/name loads and the kernel's namespace lookups. Measure -S -c pass, -S -c 'import os' and a module-level name-load loop with instruction counts before and after.

