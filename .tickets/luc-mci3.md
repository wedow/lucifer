---
id: luc-mci3
status: open
deps: []
links: []
created: 2026-11-04T17:53:58Z
type: feature
priority: 1
assignee: Greg Wedow
---
# C-stack-depth checks like CPython 3.14, replacing kernel recursion headroom

luc-3s4d gave kernel frames 50 frames of headroom past the Python recursion limit (LU_KERNEL_HEADROOM) because CPython runs that code in C, uncounted. CPython 3.14 bounds native recursion by the stack pointer (per-thread stack bounds, _Py_ReachedRecursionLimitWithMargin, Py_C_STACK checks). Give each task/carrier stack bounds, check them where native code recurses (marshal.loads segfaults at 1999-deep lists with a 512 KiB stack, as the oracle does; repr/eq/hash of nested containers), and stop charging kernel frames to the Python limit so ExceptionGroup.split/subgroup at depth 3000 succeeds as in CPython. Remove the fixed headroom.

