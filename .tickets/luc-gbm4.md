---
id: luc-gbm4
status: closed
deps: []
links: []
created: 2026-11-17T17:55:56Z
type: bug
priority: 2
---
# jit: one stop per stale-unit release, not one per leaving call

Fix-forward of 75b76d5 (review): every call leaving a stale unit stopped the world, so N threads in a killed unit cost N stops per kill (left_fanout 16 threads x 20 kills: 245 stops, 2.2 s stopped). Also lu_jit_unit_kill(u,1) set release_due for a unit already stale.


## Notes

**2026-11-17T18:17:09Z**

left_fanout.py (20 kills) stops: 4 threads 75b76d5 72-78 (79-93 ms stopped) -> 26 (5 ms); 16 threads 245-248 (2.2 s) -> 26 (5.5-6.5 ms); before 75b76d5: 5. global_rebind_frees passes on/always. floor quick jit+deopt PASS (kernel on/jit/deopt 768/768), check-capi jit=always PASS.
