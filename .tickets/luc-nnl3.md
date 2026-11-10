---
id: luc-nnl3
status: closed
deps: []
links: []
created: 2026-11-09T02:01:52Z
type: bug
priority: 1
parent: luc-vxsk
tags: [jit]
---
# jit: deopt stress aborts with 'kept frames do not match the exit' when a kernel helper raises

Found in luc-vxsk. Making the kernel's failed-lookup AttributeError positionally (e = AttributeError(text); e.name = name; e.obj = obj; raise e, in object._no_attribute or in a helper it calls), so that r1pk's lowered kernel class call makes it, made -X lucifer_jit=always -X lucifer_stress=deopt:N (N = 4, 5, 6 or 7) abort at startup: importlib._bootstrap find_spec line 1153, a keyword call of type(sys.implementation) whose path fails a lookup, Fatal 'lu_jit_deopt: lucifer jit: kept frames do not match the exit' (tests/kernel/jit_float_island.py shows it). The same code in a user function does not abort; the kwargs form (not lowered) did not either. The branch raises it from a C primitive (_lucifer.attribute_error), so the repro is the positional form in object._no_attribute.

## Notes

**2026-11-09T07:44:47Z**

Master 4a9dd60 shows it in user code too: def mk(t, n, o): e = AttributeError(t); e.name = n; e.obj = o; raise e, called in a try/except AttributeError 300 times, aborts under -X lucifer_jit=always -X lucifer_stress=deopt:9 (and 10, 11) with the same 'kept frames do not match the exit'.

**2026-11-10T03:44:42Z**

Root cause: a lowered class call inlines type.__call__ and __init__ after an allocation call that may keep its frames (a frame object of them, or deopt stress); the next exit is inside __init__, deeper than the kept frames, and lu_jit_deopt required an exact match. The kept frames are now the exit's outermost ones and the deeper frames are pushed above them. tests/kernel/jit_deopt_raise runs under deopt:1..12.
