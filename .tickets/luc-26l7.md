---
id: luc-26l7
status: open
deps: []
links: []
created: 2026-11-21T09:52:03Z
type: task
priority: 2
assignee: Greg Wedow
---
# startup: class creation's _set_names and kernel type install run as interpreted Python

luc-w2ra attribution (11-21): per class steady cost 266k instructions vs CPython's 89k. After fc0a83e8 moved update_slots to a primitive, the next run-once pieces are type.__new__'s _set_names (~20 LOC, 1-3M at startup) and the kernel class install kernel_type.install / _kernel_value (~150 LOC, ~86k bytecodes at boot). Move them to primitives mirroring CPython's type_new where that removes interpreted work, keeping __set_name__ and __init_subclass__ semantics and error messages identical. Measure per-class cost and -S -c pass.

