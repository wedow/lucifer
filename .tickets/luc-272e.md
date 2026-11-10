---
id: luc-272e
status: open
deps: []
links: []
created: 2026-11-10T02:35:04Z
type: task
priority: 2
parent: luc-zelv
---
# Kernel customization: adaptive inline budget charged after folding; exits before effects at the user instruction

Design sections 3 and 6 not built yet: the budget is still code units compiled (folded branches are not counted) with fixed INLINE_MAX_*; intrinsics are not exempt; guards before any effect in an inlined kernel dispatcher still rebuild kernel frames instead of restarting the user instruction (the island). Also the stage-2 policy cost float 0.92 -> 1.03 and raytrace 1.50 -> 1.60 of CPython: find which inlines got worse.


## Notes

**2026-11-10T05:41:04Z**

Measured in luc-zelv stage 2: letting kernel frames make any number of calls at any depth spent the inline budget early (jitcheck calls/kclasses/iterators/forward counts grew) and Python-running helpers past the call limits made deopt:N tracebacks one frame short in jit_inlined_frames (not root-caused). Landed policy: helpers cost a call (one, one deep; kernel code two deep), except in FOR_ITER step frames. A budget charged after folding would let kernel dispatchers inline with all they call; find why extra lent frames shifted the recursion depth first.
