---
id: luc-bzbq
status: open
deps: []
links: []
created: 2026-11-07T16:00:23Z
type: task
priority: 3
tags: [jit, perf]
---
# jit: staticmethod and classmethod loads through an instance compile generic

Found by luc-keyu's census: Richards' self.append (a staticmethod of _RBObject) is a generic LOAD_ATTR, since the specializer caches no descriptor with __get__; in an inlined frame it side-exits and marks device_in_add/work_in_add noinline. A specialization for an exact staticmethod (answer: its __func__, by the type's version and the instance's shadowing check) and classmethod would make them method loads that inline.

