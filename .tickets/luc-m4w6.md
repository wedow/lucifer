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


## Notes

**2026-11-08T16:21:38Z**

Root cause, from the luc-fwnf core: the reader ran BINARY_OP on C.k's value, an int sitting on the reader's own int freelist (its lv_tag held the freelist link) with a count of 2. The type attribute cache names values without owning them. _type_set cleared C's tag, stored, and cleared it again; a lookup in between assigned a new tag and cached the old value, the store freed it, and the tag stayed current until the second clear. The value went through the reader's freelist; a later cache hit's try-incref succeeded on it there, because a freelisted int kept its owner's id and the owner's fast path increments the local count unchecked; the int was then handed out again while still in use. Fix: a store into a type's dict releases the replaced value after lu_type_modified (lu_ns_exchange); tags are assigned and taken back under type_lock, which orders the two; a hit keeps its reference only while the tag is current; freelisted objects drop their owner. Kernel setters that wrote type dicts directly use instance_store. Reproduction: natural rate too low to measure (master 0 in 3000 runs at 16 at once, 0 in 200 runs of a 6-reader, 4x-round variant; the one known hit is the luc-fwnf make check). With a 100 us sleep inserted between the store and the tag clear: master 118 SIGSEGV in 144 runs (same backtrace as the luc-fwnf core), fixed 0 in 288. Fixed build without the sleep: 0 in 2000. No reliable test without a runtime hook, so none added. Follow-up: luc-55va.
