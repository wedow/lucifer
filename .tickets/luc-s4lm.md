---
id: luc-s4lm
status: open
deps: []
links: []
created: 2026-11-08T01:02:06Z
type: task
priority: 2
parent: luc-0ivh
tags: [jit, perf]
---
# JIT: resume generators in line when their bodies call Python; virtual generator objects

From luc-0ivh. inline_resume (ssa_build.c) resumes a known generator's body in the consumer only when the body runs no Python (calls_out): a call needs the generator's own frame pushed as a lent frame (lu_jit_frames_push refuses gen frames), with frame_state, exc_info and the recursion count as start() sets them. Most template and Flask generators call out, so they still resume through their own unit (lu_jit_resume). Also the generator object and its frame copy are still made per call of a generator expression (lu_jit_new_gen): when it does not escape, escape analysis (luc-k5az) could keep it virtual and the frame's slots in registers.


## Notes

**2026-11-08T08:03:41Z**

luc-esc2 did not take this on. Related, landed: a generator's loops that contain no yield now use fresh virtual loop iterators (commit 56bfd63); loops that yield keep a real iterator, since gen_yield stores the stack back to the frame.
