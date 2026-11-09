---
id: luc-9ags
status: closed
deps: []
links: [luc-uexr, luc-m3wi, luc-p2yt, luc-o8i6, luc-6ljp, luc-j2vs, luc-4yfw, luc-n6yj, luc-r1pk, luc-mltz, luc-xtmp, luc-po0x]
created: 2026-11-08T14:06:52Z
type: task
priority: 3
tags: [jit, perf]
---
# jit: feed polymorphic attribute caches from loads that left inlined frames

luc-p2yt binds a staticmethod or classmethod loaded through an instance only when the instance's type is known at compile time (a made object, a guarded type). A method's self is usually known only by a version guard, which names no type, so self.static() in an inlined method stays a generic LOAD_ATTR, which side-exits in an inlined frame and marks the method noinline (AWFY Richards' self.append). The polymorphic cache that would answer it is fed only by the generic helper in a root unit. Let the interpreter's generic LOAD_ATTR, at a site whose code has a pic, note the shape it met (including an exact staticmethod or classmethod, cached by its function), so that the recompiled inlined frame tests for it.


## Notes

**2026-11-08T22:04:06Z**

luc-r1pk: the interpreter's generic LOAD_ATTR (attribute form) and STORE_ATTR note the shapes they meet in the site's polymorphic cache when the code has compiled units (lu_jit_pic_note_interpreted), so the inlined frame that left tests them. Method-form loads are left out: noting them crashed builds in load_attr_poly's tail duplication (luc-6ljp), so self.static() through an instance (Richards' self.append) waits on that ticket. Lane jitcheck inlinedpoly (attribute loads at a 3-class site in an inlined method).
