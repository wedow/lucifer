---
id: luc-hhnm
status: open
deps: []
links: []
created: 2026-11-09T02:01:52Z
type: task
priority: 2
parent: luc-vxsk
tags: [jit, perf]
---
# generators: delegating frames of a yield from chain resume in the interpreter

From luc-vxsk: Flask's in-process run takes about 95 interpreted generator resumes per request (1.7M of 3.25M, -X lucifer_jit_stats), almost all Jinja's root and base-template generators sitting in 'yield from' while block_body yields; each item passes an lu_eval activation and interpreted SEND/YIELD_VALUE per level. gen_throw already relinks a whole chain at once (prim_gen_throw); a send could resume the innermost generator directly with the outer frames relinked (lu_gen_relink) and suspended again on a yield, resuming the parent only on the inner's return. Related: luc-s4lm.

