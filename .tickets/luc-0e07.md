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

**2026-11-04T22:03:06Z**

faulthandler_later_threads: the oracle (CPython) times out under load (1 of 5 isolated runs, and once in make check); lucifer output is correct. Needs a longer oracle bound or a less timing-bound program.

**2026-11-04T22:57:31Z**

Fixed on branch luc-0e07: threads_basic (threading.local lock now covers dict creation and __init__; 25/48 failing under load before, 0/48 after, new threads_local_race), threads_signal_sort (see luc-bx6c), faulthandler_later_threads (the free-threaded oracle itself deadlocks in join about 1 run in 25 when threads arm and cancel at once; arm+cancel now serialized by a lock; oracle 0/150 hung after). unicodedata_module: 6 s on plain lanes, 59 s alone on handoff against its 600 s bound, left alone. trace_opcodes_threads is not addressed here.
