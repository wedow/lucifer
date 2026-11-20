---
id: luc-uqfg
status: closed
deps: []
links: []
created: 2026-11-19T12:00:49Z
type: task
priority: 3
---
# truth of an instance whose class has no __bool__ or __len__ is folded

unpickle_pure_python: _Unframer.read's 'if self.current_frame' (a BytesIO) and similar tests call lu_truth_exact out of line (1.5% with lu_jit_truth 0.8%). Under the class's version guard the truth of an instance of a class with neither __bool__ nor __len__ is True: fold it like IS_OP's known types.


## Notes

**2026-11-20T11:25:58Z**

The JIT folds truth to `True` for known exact classes whose MRO has neither
`__bool__` nor `__len__`. The class version guard invalidates the fold when a
base gains either method. The growth check failed before the fix with 45,787
and 85,787 generic truth operations at 20,000 and 40,000 iterations. After the
fix it recorded 5,963 at both sizes. The kernel program matches CPython when
inherited methods change during the loop and when `__bool__` returns the wrong
type. It also passes under forced guard failures.

**2026-11-20T16:58:26Z**

All four gates passed. The quick floor lost zero recorded passes, and the kernel suite passed 794 checks in each of the plain, JIT, and forced-deoptimization lanes.
