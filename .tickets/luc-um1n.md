---
id: luc-um1n
status: closed
deps: []
links: []
created: 2026-11-18T18:42:04Z
type: task
priority: 1
---
# jit: a release checks the frames where it calls C

Gap report 2026-11-18 mechanism #1. A release that may run code (noisy DECREF) is followed by GUARD_FRAMES even when it took the fast path and called nothing. Check the frames, and unit->dead when the unit has dependencies, in the release's own slow path; facts.c then drops the GUARD_FRAMES and no longer counts a release as unsettling.


## Result

Landed. Executed GUARD_FRAMES, 3 AWFY iterations, before -> after: Richards 557 M -> 314 M, List 225 M -> 31 M,
Permute 292 M -> 162 M, Havlak 319 M -> 169 M, DeltaBlue 68 M -> 0, CD 154 M -> 91 M, Queens 87 M -> 35 M. Steady
instructions per iteration: List 0.955, DeltaBlue 0.945, Richards 0.984, Havlak 0.983; AWFY cycles geomean 0.979.
Gates on this commit (with luc-5b0q under it): floor quick PASS; kernelcheck jit-deopt 775/775; jit 773/775 with
threads_container_protocol and threads_local_race timing out on a loaded host, both passing alone on this commit
(11 s) and on master (23-31 s); check-jit PASS.
