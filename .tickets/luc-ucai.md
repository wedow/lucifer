---
id: luc-ucai
status: closed
deps: []
links: []
created: 2026-11-06T21:49:51Z
type: task
priority: 3
assignee: Greg Wedow
---
# JIT: compile LOAD_SUPER_ATTR, FORMAT_SIMPLE, RAISE_VARARGS, module-level loops; recompile on hot side exit

Leftovers from luc-jp30. Module code is not marked optimized so module-level loops never compile (292 vs 196 ns/iter CPython). Pruned-branch flip recovers only after 50 guard failures; guard exits are small today (Flask ~2.9k/run) so low priority.


## Notes

**2026-11-08T04:41:12Z**

Policy part done on luc-policy (luc-xboo): any dead or retired unit is recompiled after its wait, so a side exit that a recompile can fix is fixed; FORMAT_SIMPLE/FORMAT_WITH_SPEC/CONVERT_VALUE/BUILD_STRING/BUILD_SLICE/STORE_GLOBAL now compile (base64 side exits 3.16M -> 2k, pprint 0.6M -> 0.9k). Module-level loops split to luc-qpu2.
