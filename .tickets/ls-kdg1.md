---
id: ls-kdg1
status: open
deps: []
links: []
created: 2026-11-24T08:25:21Z
type: bug
priority: 3
assignee: Greg Wedow
parent: luc-qtqf
---
# super().__class__ walks the MRO; __class__ cell read without a reference at compile time

From the review of the super() fold. (1) CPython's super_getattro skips the MRO walk for the name __class__, so super().__class__ is the super type; lu_super_lookup (runtime/prim/type.c) has no such skip, and the interpreter's LOAD_SUPER_ATTR, lu_super_attr_native and the compile-time fold all use it. Seen only when a base defines __class__ (class B: __class__ = 5; class C(B): def f(self): return super().__class__ gives 5, CPython the super type): otherwise object's getset is found and the fold declines. Fix in lu_super_lookup with a kernel test. (2) root_class_cell (runtime/jit/eval_ops.c) reads the cell's value without a reference before taking one as a constant; another thread rebinding the cell and dropping the last reference to the class at compile time could free it. Take the reference under the cell's critical section. (3) class_attribute and super_known leave a guard and dependency behind when they decline after recording them: extra invalidations only. (4) super_known's learned-type branch: the interpreter records no profile at LOAD_SUPER_ATTR, so check that the profiling helper is what teaches it and that the comment says so. (5) The instance-attribute sites in runtime/jit/eval_attr.c that read a descriptor's type slots at compile time: confirm each relies on the value's class version as lu_ev_plain_value does.

