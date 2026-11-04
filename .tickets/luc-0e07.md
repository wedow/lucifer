---
id: luc-0e07
status: open
deps: []
links: []
created: 2026-11-03T15:56:37Z
type: bug
priority: 1
tags: [test, threads]
---
# Flakes under host load: threads_basic and threads_signal_sort failed once under LANE=handoff (0/4 on rerun); unicodedata_module times out near its limit (~98 s alone)

Treat as a regression until explained (noise is a regression). Capture the failing diff with a loop under load, find whether a test depends on timing or the runtime has a race; shrink unicodedata_module or make it sample code points so it runs well inside the timeout.


## Notes

**2026-11-04T05:56:58Z**

trace_opcodes_threads (timing-ratio test from the luc-2vif review) failed 2 of 5 runs under LANE=handoff during the luc-00js review. Make it schedule-independent: count stops-the-world or events instead of timing.

**2026-11-04T17:19:30Z**

threads_basic's threading.local check is a runtime race, not timing: 200 rounds of a threading.local subclass with __init__ read by three new threads (the threads_basic block in a loop) raise AttributeError: 'Mine' object has no attribute 'value' in 1-3 rounds per run, on master as well as luc-sx8l. A thread's first access to the local sometimes misses the value its __init__ just set.
