---
id: lu-r6wr
status: open
deps: []
links: []
created: 2026-11-23T02:06:08Z
type: bug
priority: 2
tags: [semantics, finalizers]
---
# finalizers of attributes of an instance whose class was reassigned and whose __dict__ was deleted run at other times than CPython's

On origin/master (dd9ce457). Program:

    class D:
        def __init__(self, k): self.k = k
        def __del__(self): log.append(self.k)
    class Wide:
        def __init__(self):
            for k in range(40): setattr(self, 'a%d' % k, k)
    Wide()
    class Narrow:
        def __init__(self, v): self.v = v
    log = []
    def moved(n):
        out = [None]
        for i in range(n):
            o = Narrow(D(i))
            o.__class__ = Wide
            del o.__dict__
            out[0] = o
            o = None
        out[0] = None
        return len(log)
    print(moved(500))

Each D should be finalized when del o.__dict__ drops the dict (its only holder), so CPython's count should be close to 500; it printed 247 under the jit lane's flags and 248 under the plain lane's, so CPython itself is not exact here (the dict's release may be left to its collector). Lucifer printed 485 (jit) and 416 (plain). A following case that counted 3 finalizers per iteration over 2000 iterations (5999 in CPython) counted 6014 and 6083 on Lucifer after the log was cleared, which looks like finalizers of this case's objects running later; not verified. To settle: whether del __dict__ releases the dict at once on each runtime, and when the remaining D objects die (gc.collect() before counting). jit_release_shapes.py (luc-uqyj) now only checks that this case completes.

