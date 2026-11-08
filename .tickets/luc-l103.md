---
id: luc-l103
status: open
deps: []
links: []
created: 2026-11-08T13:44:31Z
type: task
priority: 2
tags: [jit, perf]
---
# jit: AWFY CD steady state hangs on compile timing (+3.6% instructions on luc-policy)

luc-xboo measured CD at 1.034x master time (199.0 vs 192.0 G instructions, cycles about +1%). The same tree without the size-scaled threshold runs 183.5 G, without the size-scaled wait 187.5 G: the units CD settles on depend on when they compile (_RedBlackTree.put@60 never gets its loop unit, _Vector2D.plus/minus compile standalone, _tree_insert recompiles 17 times). Both scalings pay for cli and import (isort compile 273 vs 509 ms). Wanted: a steady state that does not depend on the compile trajectory, e.g. a recompile of a unit whose callees' units died, or inlining decisions that do not read whether a callee is compiled yet.

