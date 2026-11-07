---
id: luc-32pn
status: open
deps: []
links: []
created: 2026-11-07T22:27:52Z
type: task
priority: 2
assignee: Greg Wedow
tags: [jit, perf]
---
# jit: compile cost is 7-8% of corpus time

Corpus census 2026-11-07: 350-800 compiles and 0.4-1.0 s per process, 50% of isort, 21% of import black, 6.7% weighted across the corpus. Options: measure compile-time phases first; then raise thresholds adaptively for one-shot code; cache compiled units across runs only if safe; compile on a background thread (roadmap item 5 deferred it until compile time showed up, and now it has). Proof: compile seconds per process and cli/import timings.

