---
id: luc-9jrr
status: open
deps: []
links: []
created: 2026-11-10T21:46:54Z
type: task
priority: 2
tags: [perf, jit]
---
# Class attributes that are neither descriptors nor immortal never specialize

A load through an instance of a class attribute that is no descriptor specializes only when the cache may name the value without owning it (immortal or deferred refcount), so a class constant like pickle's _Framer._FRAME_SIZE_TARGET (65536) or a class dict (_Pickler.dispatch) stays generic: 15.7k and 10k generic loads in 3 loops of pickle_pure_python on luc-custom4. ints and strs cannot be deferred (not GC objects). Compiled code could fold the value as a constant it owns (konst) under the class's version (the type's watcher already invalidates), which needs the PIC or the cached site to name the class rather than the value.

