"""Per-operation call ladder. Usage: call.py CASE WARM REPS; a rep is N iterations of the case's loop.
The recursive callees stay calls (the inliner refuses recursion): rec(K) is one call from an inlined
frame and K - 1 direct calls, so a per-call cost is the slope between two depths."""
import sys, collections
N = 20000

def rec(n, b):
    if n:
        return rec(n - 1, b)
    return b

def krec(n, b):
    if n:
        return krec(n - 1, b=b)
    return b

def srec(n, *args):
    if n:
        return srec(n - 1, 1, 2)
    return n

def skrec(n, *args, **kwargs):
    if n:
        return skrec(n - 1, 1, 2)
    return n

class C:
    def m(self, n, b):
        if n:
            return self.m(n - 1, b)
        return b

def small_gen(n):
    for i in range(n):
        yield i

def calling_gen(n, x):
    for i in range(n):
        rec(1, x)
        yield i

def empty(x):
    for i in range(N):
        pass

def mk(fn, k):
    def loop(x):
        for i in range(N):
            fn(k, x)
    return loop

rec0, rec1, rec2, rec12 = mk(rec, 0), mk(rec, 1), mk(rec, 2), mk(rec, 12)
krec2, krec12 = mk(krec, 2), mk(krec, 12)
srec2, srec12 = mk(srec, 2), mk(srec, 12)
skrec2, skrec12 = mk(skrec, 2), mk(skrec, 12)

def meth2(x):
    o = C()
    for i in range(N):
        o.m(2, x)

def meth12(x):
    o = C()
    for i in range(N):
        o.m(12, x)

def replace(x):
    s = "12345"
    for i in range(N):
        s.replace("&", "&amp;")

def dget(x):
    d = {"a": 1, "b": 2}
    for i in range(N):
        d.get("b")

def lappend(x):
    l = []
    for i in range(N):
        l.append(x)

def dqappend(x):
    w = collections.deque().append
    for i in range(N):
        w(x)

def isinst(x):
    for i in range(N):
        isinstance(x, str)

def isinst_t(x):
    t = (int, float)
    for i in range(N):
        isinstance(x, t)

def gen_fused(x):
    for i in small_gen(N):
        pass

def gen_call(x):
    for i in calling_gen(N, x):
        pass

SEQ = list(range(N))

def double(v):
    return v + v

def enum(x):
    for i, v in enumerate(SEQ):
        pass

def zip2(x):
    for a, b in zip(SEQ, SEQ):
        pass

def mapf(x):
    for v in map(double, SEQ):
        pass

SHORT = [3, 1, 4, 1, 5, 9, 2, 6]

def enum8(x):
    for i in range(N):
        for j, v in enumerate(SHORT):
            pass

def zip8(x):
    for i in range(N):
        for a, b in zip(SHORT, SHORT):
            pass

def map8(x):
    for i in range(N):
        for v in map(double, SHORT):
            pass

def gen8(x):
    for i in range(N):
        for v in small_gen(8):
            pass

def attr(x):
    o = C(); o.v = x
    for i in range(N):
        o.v

if __name__ == "__main__":
    f = globals()[sys.argv[1]]
    warm, reps = int(sys.argv[2]), int(sys.argv[3])
    for i in range(warm + reps):
        f("x")
