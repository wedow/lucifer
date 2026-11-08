---
id: luc-xtmp
status: closed
deps: []
links: []
created: 2026-11-08T17:30:35Z
type: task
priority: 3
assignee: Greg Wedow
tags: [scaling]
---
# type_lookup: MRO walk increfs a shared tuple on every uncached lookup

luc-55va's fix keeps the MRO tuple alive across a walk with an atomic incref/decref, because a dict lookup in the walk can wait on a lock and detach the carrier, which QSBR treats as holding nothing. Cache hits are free, but classes that never get a tag again (1000 tags spent, custom mro()) pay a contended atomic per lookup on a tuple all threads share. Remove it: keep the carrier attached (or count it as holding the MRO epoch) across lock waits inside the walk, or give such classes a per-thread lookup cache, then measure a many-thread lookup microbenchmark at 1/8/32 threads.


## Notes

**2026-11-08T22:04:06Z**

Done on luc-r1pk: lookup_in_mro walks with the dicts' lock-free lookup (lu_dict_find_lockfree), which neither waits, detaches nor releases, so the shared MRO tuple needs no reference; it takes one only when a lookup must lock. Threaded microbenchmark (N threads looking a class attribute up through a class whose tags are spent, million lookups/s in total, best of 3, exclusive bench host): master 30.5 / 110.3 / 392.3 at 1 / 8 / 32 threads, branch 38.3 / 143.1 / 443.2, CPython 3.14t 25.4 / 87.4 / 159.6.
