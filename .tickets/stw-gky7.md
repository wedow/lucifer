---
id: stw-gky7
status: open
deps: []
links: []
created: 2026-11-17T18:47:24Z
type: bug
priority: 2
assignee: Greg Wedow
---
# delayed release decrefs (and may finalize) while the world is stopped

lu_qsbr_retire on a delayed release taken during a stop-the-world decrefs objects at once, so a finalizer can run while the world is stopped. Found in the luc-4uge/jpp9 review (5e65c7f, a204ebe). Hand the objects to the collector or a post-restart pass instead.

