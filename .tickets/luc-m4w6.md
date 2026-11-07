---
id: luc-m4w6
status: open
deps: []
links: []
created: 2026-11-07T14:39:12Z
type: bug
priority: 1
assignee: Greg Wedow
tags: [jit, threads]
---
# threads_cache_churn: rare SIGSEGV in the jit lane

luc-fwnf's second make check hit one SIGSEGV in tests/kernel threads_cache_churn (jit lane); 0 in 430 reruns of that build, master 0 in 240. Matches the ~1% churn crash luc-nvpl closed, so that fix is incomplete or a second race exists. Reproduce under load (parallel runs with the host busy, rr or core dumps), find the race, fix it.

