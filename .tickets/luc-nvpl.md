---
id: luc-nvpl
status: open
deps: []
links: []
created: 2026-11-06T03:12:57Z
type: bug
priority: 1
assignee: Greg Wedow
---
# threads_cache_churn crashes ~1% of runs under contention

Seen on master ec1ea11 and luc-6e6n: 4 SIGSEGV in 480 runs of tests/kernel/threads_cache_churn.py on master; other failures are stack-smash abort and "'tuple' object is not callable". Looks like a use-after-free in inline-cache churn. Reproduce with a loop, bisect the cache/refcount rule (luc-m0na: caches point only at immortal or deferred-refcount objects, registered in lu_specialize_forget).

