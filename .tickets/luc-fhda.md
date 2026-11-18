---
id: luc-fhda
status: closed
deps: []
links: []
created: 2026-11-18T02:25:32Z
type: task
priority: 1
---
# jit: rely on a type version the path already read

Gap report 2026-11-18 mechanism #1, pass 1. The evaluator guards an object's type version at every attribute it loads: Richards ran 2.6 G GV_TYPE_OF guards per 10 iterations (the old middle end, f9c8284^, 0.63 G: it made dominated guards dependencies). Keep the version as a fact of the value; a repeated guard of it, or one on a type the path knows, is a dependency of the unit (facts.c).


## Result

Landed: a repeated GV_TYPE_OF guard is INS_PROVEN and becomes a dependency (facts.c); a known type's guard is a
dependency too. Executed GV_TYPE_OF guards, 3 iterations each, before -> after: Richards 780 M -> 209 M, Json 138 M ->
10 M, Bounce 268 M -> 23 M, NBody 231 M -> 20 M, Towers 221 M -> 39 M, Havlak 217 M -> 76 M, CD 219 M -> 101 M,
Queens 103 M -> 12 M, Storage 98 M -> 25 M, DeltaBlue 56 M -> 37 M. AWFY steady cycles geomean 0.972 on a core
lease (Richards 0.887, Bounce 0.851, NBody 0.859); instructions 0.977 over AWFY and the eight pyperformance rows.
