---
id: lu-ex68
status: open
deps: []
links: []
created: 2026-11-23T00:45:58Z
type: bug
priority: 2
tags: [jit, semantics]
---
# jit: a virtual tuple's items are released first to last, CPython releases them last to first

A tuple compiled code keeps virtual and drops releases its items in index order; tuple_dealloc (and CPython) releases them from the last to the first, so finalizers of the items run in the opposite order. Reproducer, on origin/master in the plain and jit lanes: in a hot loop, t = (Item(i), Item(-i)); t = None with Item.__del__ logging its key; CPython logs -i then i, Lucifer i then -i once the loop is compiled (both lanes, from the first compiled iterations). Found while writing the luc-uqyj release tests; the release order of a virtual object's fields should follow the dealloc of the type it stands for.

