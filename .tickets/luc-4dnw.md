---
id: luc-4dnw
status: open
deps: []
links: []
created: 2026-11-18T02:25:32Z
type: bug
priority: 1
---
# threads_cache_churn segfaults about 1 in 15 runs in the jit-deopt lane on master

Seen 2026-11-18 on origin/master 4630a70 (no local change): kernelcheck --lane jit-deopt --programs threads_cache_churn, 1 of 15 runs exits -11; once more in a gate run. Earlier tickets luc-m4w6, luc-nvpl, luc-m0na are closed.


## Notes (2026-11-19 race lane)

threads_cache_churn in the jit-deopt lane, runs exiting 139: master 1929ea93 1 of 100 (core: the reader thread's int freelist held a bad pointer when hot() allocated an int, so an int was freed while still in use earlier; no unit of hot was left to read, so the cause is not tied); with luc-knb1, luc-349f and luc-4t4e 0 of 100. luc-349f fits the family (hot's units are killed by the writer while they run, and a return through the broken exit returned what lay below its stack), but without a core that shows it this stays open until a run reproduces it again.
