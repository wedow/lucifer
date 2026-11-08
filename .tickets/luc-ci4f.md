---
id: luc-ci4f
status: in_progress
deps: []
links: []
created: 2026-11-08T18:22:25Z
type: task
priority: 1
tags: [jit, perf]
---
# jit: cold code and compile cost (census mechanisms 1 and 2)

Census 2026-11-08 mechanisms 1 (interpreter fallback, 8.9%) and 2 (compile cost, 8.7%). Interpreted module/cold code (luc-qpu2), interpreted generator resumes (luc-s4lm, luc-j7zk, luc-9e70), the retire rule that leaves kernel units interpreted (BufferedReader.read, _lru_cache_wrapper.wrapper, chain.__next__, _asend_send), compile cost on cli programs and generated regex matchers (regex_v8, regex_compile), background compile (luc-f7cn) if justified. Primary target: the async_tree family.

## Acceptance Criteria

Interpreted resumes, retired units and compile seconds per process before/after; async_tree family, regex_v8/regex_compile, cli suite and pyperformance geomean master vs branch vs 3.14t best of 3; AWFY no regression; five gates 0.

