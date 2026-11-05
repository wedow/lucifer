---
id: luc-gevw
status: open
deps: []
links: []
created: 2026-11-05T10:37:47Z
type: bug
priority: 2
tags: [perf, numpy]
---
# numpytest shards past their deadline: multinomial repeat and the mem_overlap fuzz

In the 2026-11-05 full run two shards hit the 1,100-second deadline: the one holding test_randomstate.py (test_integer_repeat[multinomial] takes about 430 s alone, 0.5 s on CPython 3.14t; JIT on and off alike) left 576 rows of test_typing, test_overrides, test_pocketfft and testing/test_utils NOTRUN, and the slowest test_mem_overlap fuzz test left 108. Find what multinomial spends its time on; the deadline only hides it.

