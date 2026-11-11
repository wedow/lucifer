---
id: luc-x1k7
status: open
deps: []
links: []
created: 2026-11-11T22:44:53Z
type: bug
priority: 2
assignee: Greg Wedow
tags: [jit]
---
# jit: iter_step loop exits may lack the frames guard

ssa_build.c iter_step (~5378-5404, inlined next_item for per-class arms): both loop exits (jump_to exit_pc from last_done; maybe -> edge_to exit_pc) leave without a parent-level frames guard. Same shape as the FOR_ITER exit-edge bug fixed in 9687477. Check whether tests/diff/corpus/loop_end_frame_write.py compiles through type_arms -> iter_step or H_for_iter; add a case that forces iter_step with a finalizer writing the looping frame's f_locals at loop end.

