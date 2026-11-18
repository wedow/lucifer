---
id: luc-ibnp
status: open
deps: []
links: []
created: 2026-11-18T22:46:29Z
type: bug
priority: 2
tags: [jit, perf]
---
# jit: a boxed read of an unboxed field costs a critical section

Accepted loss of luc-ihjn. A field stored unboxed (LU_UNBOXED) that a reader needs as an object (an argument of a call not taken in line, a container store, the interpreter's LOAD_ATTR) gets its box made and installed by lu_inline_get under the owner's critical section, so every later reader sees the same object. Where most fields are read so once, that costs more than the float the store no longer made: CD +1.9% instructions (PyCriticalSection_Begin/End +153 M, PyFloat_FromDouble +68 M, lu_inline_get +46 M per 3 iterations; Vector2D fields passed to _compare_numbers), pyperformance raytrace +3.4% (the same symbols, +8 M). A fresh box per read would break identity (o.x is o.x), and installing by compare-and-swap races writers, which store the raw word and the marker apart. Ways: install without PyCriticalSection's bookkeeping when the lock is free; or let a store site that sees its fields read boxed store objects (feedback per type and offset).

## Acceptance Criteria

CD and pyperformance raytrace no longer lose instructions to luc-ihjn on a core lease; identity and the free-threaded write rules unchanged.

