---
id: luc-9ags
status: open
deps: []
links: [luc-uexr, luc-m3wi, luc-p2yt, luc-o8i6]
created: 2026-11-08T14:06:52Z
type: task
priority: 3
tags: [jit, perf]
---
# jit: feed polymorphic attribute caches from loads that left inlined frames

luc-p2yt binds a staticmethod or classmethod loaded through an instance only when the instance's type is known at compile time (a made object, a guarded type). A method's self is usually known only by a version guard, which names no type, so self.static() in an inlined method stays a generic LOAD_ATTR, which side-exits in an inlined frame and marks the method noinline (AWFY Richards' self.append). The polymorphic cache that would answer it is fed only by the generic helper in a root unit. Let the interpreter's generic LOAD_ATTR, at a site whose code has a pic, note the shape it met (including an exact staticmethod or classmethod, cached by its function), so that the recompiled inlined frame tests for it.

