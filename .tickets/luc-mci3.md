---
id: luc-mci3
status: closed
deps: []
links: []
created: 2026-11-04T17:53:58Z
type: feature
priority: 1
assignee: Greg Wedow
---
# C-stack-depth checks like CPython 3.14, replacing kernel recursion headroom

luc-3s4d gave kernel frames 50 frames of headroom past the Python recursion limit (LU_KERNEL_HEADROOM) because CPython runs that code in C, uncounted. CPython 3.14 bounds native recursion by the stack pointer (per-thread stack bounds, _Py_ReachedRecursionLimitWithMargin, Py_C_STACK checks). Give each task/carrier stack bounds, check them where native code recurses (marshal.loads segfaults at 1999-deep lists with a 512 KiB stack, as the oracle does; repr/eq/hash of nested containers), and stop charging kernel frames to the Python limit so ExceptionGroup.split/subgroup at depth 3000 succeeds as in CPython. Remove the fixed headroom.


## Notes

**2026-11-21T07:42:42Z**

Superseded by luc-s3tf and done by 297f55f2 + 585436fb: tasks carry C stack bounds, C entries into the interpreter and Py_EnterRecursiveCall check the stack pointer, kernel frames no longer count against the recursion limit (their room is sized from the stack), and the fixed headroom is gone. ExceptionGroup.split/subgroup at depth 3000 succeed in jit off and jit as in CPython. marshal.loads of a 1999-deep list in a 512 KiB thread segfaults on the oracle too, so it is not a parity target.
