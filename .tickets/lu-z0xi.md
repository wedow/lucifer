---
id: lu-z0xi
status: open
deps: []
links: []
created: 2026-11-23T02:06:08Z
type: bug
priority: 2
tags: [semantics, refcount]
---
# refcount of a class whose instances hold it in attributes stays 2 above CPython's after the instances die

On origin/master (dd9ce457), plain and jit lanes. Program:

    import sys
    class S:
        def __init__(self):
            self.k = type(self)
            self.j = type(self)
    def f(n):
        out = [None]
        c = sys.getrefcount(S)
        for i in range(n):
            out[0] = S()
        out[0] = None
        return sys.getrefcount(S) - c
    print(f(2000))

CPython 3.14t prints 0; Lucifer prints 2 in both the plain and the jit lane (so not compiled code alone). Not established: whether the 2 are references something keeps for good (a cache, a specialization record, an attribute-order record holding the class) or ones released later; the count does not grow with n in the one size tried. Found writing tests/kernel/jit_release_shapes.py (luc-uqyj), whose own-class case only checks the delta is not negative because of this.

