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

**2026-11-09T04:45:21Z**

luc-vxsk: Flask's in-process run still takes about 95 interpreted generator resumes per request, Jinja's delegating root generators (luc-hhnm).

**2026-11-10T17:17:13Z**

luc-custom3: set(genexpr) now runs the generator in its loop (fc47098: a set.__init__ twin, native set adds, get_iter of a generator folded at any depth, the last inline level open to a resumed generator, tail calls one level deeper), so nqueens resumes 1.27M fewer generators per run (2059M -> 1460M instructions per loop). Not done here: calls from a resumed body. lu_jit_frames_push/pop and the kept-frame exit would take the generator's own frame (relink on push, unlink on pop, no code/func incref when kept, the NEXT entry popped off its caller's stack), and calls_out would allow IF_PY; but in the case that matters (struct's _unpack, tuple(genexpr) over _unpack_item) the generator sits at depth 3 and its call is refused by the opaque-call depth limit for user code (1), and every call would push the frames out of line, so whether it beats the generator's own unit is unmeasured. The generator object is still made per genexpr call (virtual generators not started).

**2026-11-10T21:46:59Z**

luc-custom4: the opaque-call bounds are now three frames deep (four in kernel code) with up to four calls per inlined frame (c8b87e6; mako -13%, raytrace -4%, telco/pickle/unpickle -3 to -4%). Measured before taking the rest: generator resumes through their own unit are ~10k per loop in telco, ~47k in nqueens, ~30k in raytrace, under 1% of each at ~200 instructions a resume, and the lent frames pushed for calls from inlined frames are 2-6% of instructions (mako 558k pushes per loop, json_dumps 400k, pickle 187k, ~55 instructions each with their pop). Neither lazy frames nor calls from resumed generator bodies pays its cost on these programs yet; the generator objects made per genexpr call remain (virtual generators not started).
