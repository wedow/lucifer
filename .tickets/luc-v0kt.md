---
id: luc-v0kt
status: open
deps: []
links: []
created: 2026-11-07T05:56:29Z
type: task
priority: 2
---
# JIT: describe inlined frames at calls instead of pushing lent frames

A call from an inlined frame still pushes every inlined frame as a lent frame (lu_jit_frames_push) and pops them after (lu_jit_frames_pop): 3-4% of AWFY Richards after luc-cmel. Give inlined frames the root's treatment: record them in the published state and let walkers (lu_frame_walk, lu_frame_user, frame objects) see them through it, materializing only when one is observed.

