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

