---
id: luc-nj9b
status: open
deps: []
links: []
created: 2026-11-21T21:00:13Z
type: task
priority: 2
tags: [perf, jit]
---
# docutils: +2.1% whole-run cycles at -0.2% instructions after 36ecfdd0

luc-tax0 stage A2 (word prims, 36ecfdd0): docutils whole run (n=4, -l loops/4, one core, min of 7, loaded host) 291.8G -> 298.0G cycles at 307.7G -> 307.0G instructions. Accepted under the net-win rule (tomli -20%, json_loads -5%, mdp -5%, unpickle -4%, deepcopy -2.5%). Check on a quiet host whether it holds; if so, count docutils compiles and compile time before and after (the new lowering learns int operand types and str/dict kinds, which may add first-compile profiling and recompiles in a compile-heavy row) and fix the cause.

