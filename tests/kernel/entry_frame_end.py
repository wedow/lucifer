# How a frame ends after a compiled return: a frame object kept past the
# return with its locals; finalizers that run at the return and walk the
# frames, rebind a global the callee folded, replace the callee's
# __code__, or raise; a frame object made and dropped in the call (its
# count 1 at the return); generators that return into for, next, list and
# sum; frames that begin a datastack chunk; and recursion that returns
# near the limit, whose depth must come back whole every time.
import sys
import types

N = 3000
kept = []


def keeps(x):
    y = x * 2
    if x % 500 == 0:
        kept.append(sys._getframe())
    return y + 1


def peeks(x):
    sys._getframe().f_lineno
    return x - 1


def frame_objects():
    t = 0
    for i in range(N):
        t += keeps(i) + peeks(i)
    print('frame objects', t, [sorted(f.f_locals.items()) for f in kept])


SCALE = 3
seen = []


class Walk:
    def __del__(self):
        f, names = sys._getframe(1), []
        while f is not None and len(names) < 3:
            names.append(f.f_code.co_name)
            f = f.f_back
        seen.append(tuple(names))


class Rebind:
    def __del__(self):
        global SCALE
        SCALE = 4 if SCALE == 3 else 3


class Recode:
    def __del__(self):
        holds_rebind.__code__ = alt.__code__ if holds_rebind.__code__ is not alt.__code__ else orig


class Raise:
    def __del__(self):
        raise ValueError('in __del__')


def holds_walk(x):
    w = Walk()
    return x + 1


def holds_rebind(x):
    r = Rebind()
    return x * SCALE


def alt(x):
    r = Rebind()
    return x * SCALE + 1000


orig = holds_rebind.__code__


def holds_recode(x):
    r = Recode()
    return holds_rebind(x)


def holds_raise(x):
    r = Raise()
    return x - 1


unraised = []
sys.unraisablehook = lambda u: unraised.append(type(u.exc_value).__name__)


def finalizers():
    a = b = c = d = 0
    for i in range(N):
        a += holds_walk(i)
        b += holds_rebind(i)
        c += holds_recode(i)
        d += holds_raise(i)
    print('finalizers', a, b, c, d, sorted(set(seen)), len(unraised), set(unraised))


def gen(n):
    for i in range(n):
        yield i
    return n


def generators():
    t = 0
    for i in range(N):
        for v in gen(i % 5):
            t += v
        t += next(gen(0), -1) + next(gen(2), -1)
        t += sum(gen(i % 7)) + len(list(gen(i % 3)))
    print('generators', t)


def wide(n, a=1, b=2, c=3, d=4, e=5, f=6, g=7, h=8, i=9, j=10, k=11, l=12, m=13, o=14, p=15, q=16):
    if n == 0:
        return a + q
    return wide(n - 1, a, b, c, d, e, f, g, h, i, j, k, l, m, o, p, q) + 1


def chunks():
    t = 0
    for depth in range(1, 400, 7):
        for _ in range(20):
            t += wide(depth)
    print('chunks', t)


def down(n):
    if n == 0:
        return 0
    return down(n - 1) + 1


def deepest():
    n = 0
    try:
        while True:
            down(n)
            n += 50
    except RecursionError:
        pass
    lo = n - 50
    while lo < n:
        try:
            down(lo + 1)
            lo += 1
        except RecursionError:
            break
    return lo


def recursion():
    sys.setrecursionlimit(600)
    depths = [deepest() for _ in range(30)]
    near = 0
    for k in range(0, 200, 3):
        near += down(depths[0] - k)
    print('recursion', len(set(depths)), deepest() == depths[0], near == sum(depths[0] - k for k in range(0, 200, 3)))


frame_objects()
finalizers()
generators()
chunks()
recursion()
