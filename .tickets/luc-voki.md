---
id: luc-voki
status: open
deps: []
links: []
created: 2026-11-19T02:36:47Z
type: task
priority: 3
tags: [jit, perf]
---
# pp float +2.6% instructions with luc-ka5h

Accepted loss of luc-ka5h (operator ways per learned type): pyperformance float steady instructions 87.0 M -> 89.2 M per iteration (1.026, 3 reps on a core lease; 1.021 and 1.024 in two 1-rep runs). The C profile is unchanged (eval_frame 12.4% vs 12.7%, the rest within 0.1%), so the difference is in compiled code: find the float sites that now take ways (Point methods' operators meeting a few classes) or IS/truth folds that changed a unit's shape, with LUCIFER_JIT_DUMP of the hot units on both builds.

