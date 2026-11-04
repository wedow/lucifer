---
id: luc-4nwl
status: closed
deps: []
links: []
created: 2026-11-03T23:40:11Z
type: bug
priority: 1
---
# GC stress lanes: collect on a real allocation budget so gc:N sets the rate

Review of luc-74bf: the skip (stress_allocs >= stress_live/2) counts polls, not allocations; gc:1 collects about as often as normal GC (a dropped cycle survives 3,842 iterations), so short traverse/refcount windows go untested. Strict gc:1 costs hours over the whole lane. Fix: count real GC allocations and collect every N; run strict gc:1 on a curated subset, and a no-skip gc:100 on the rest. Update FOUNDATIONS 2.7.


## Notes

**2026-11-04T01:38:58Z**

gc stress ticks now collect every Nth GC allocation, no skip; gc:1 runs a curated 79-program list, gc:100000 all programs; no collector bug found
