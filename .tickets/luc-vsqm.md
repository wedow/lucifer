---
id: luc-vsqm
status: open
deps: []
links: []
created: 2026-11-03T23:40:11Z
type: bug
priority: 2
---
# gen_traverse must visit frame.frame_obj; PyThreadState_Delete of a non-current task merges BRC with no task attached

Review of luc-74bf, unproven: (1) generator frame objects are not traversed (leaks cycles only); (2) lu_task_delete -> unbind -> lu_brc_retire frees objects outside stop-the-world when an embedder deletes a non-current thread state.

