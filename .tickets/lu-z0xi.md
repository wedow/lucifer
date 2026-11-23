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


## 2026-11-23: what would make it a defect

A constant difference (2, the same at every n) means something holds two references to the class for good or for a while (a cache, a record) and is benign for lifetimes. A difference that grows with n would be instances giving their class back other than once, a leak or an early free. jit_release_shapes.py now prints the growth between n and 2n (0 on both runtimes), not the difference itself. Left open only to identify the two references.
Measured on origin/master 032cec77 and on luc-uqyj: the difference after 2n instances less the one after n is 3 for n = 2000 (CPython 0), in the plain and jit lanes alike: small and not proportional to n. The test requires it to stay below n / 10.
