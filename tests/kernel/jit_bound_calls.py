# Calls whose callee compiled code learns from the site, not from a
# constant: a nested function, a closure, a function passed in or held in a
# local, called with keywords, defaults, keyword-only parameters and star
# arguments on either side. tools/jitcheck runs this at two sizes and
# requires that no call per iteration is made as C makes a keyword or star
# call: each binds its arguments at compile time, in line or not (a
# recursive callee, one too large to inline, a bound method, a generator,
# a coroutine, a function made in the loop with its defaults). The
# callees' defaults change (replaced, and changed in place, also while a
# loop runs) and the callee is replaced under the compiled calls.
# jit_bound_edges.py has the calls that do not bind.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


def make(k):
    def area(w, h=2, *, depth=1):
        return w * h * depth + k

    def fwd(*args, **kw):
        return area(*args, **kw)

    def lead(x, *args, **kw):
        return area(x, *args, **kw)

    def keep(*args, **kw):
        # The callee's own tuple and dict, which it may change.
        args = list(args)
        args.append(len(kw))
        kw['seen'] = 1
        return len(args) + len(kw)

    def acc(v, into=[]):
        into.append(v)
        return len(into)

    def order(*, a, b, c=0):
        return a * 100 + b * 10 + c

    return area, fwd, lead, keep, acc, order


def scale(x, *, by=2):
    return x * by


def trace(log, v):
    log.append(v)
    return v


def hot(n, fns):
    area, fwd, lead, keep, acc, order = fns
    s = 0
    log = []
    for i in range(n):
        s += area(i) + area(i, 3) + area(i, depth=2) + area(h=i, w=2) + area(i, depth=3, h=1)
        s += fwd(i, h=2) + fwd(i) + fwd(w=i, depth=4) + lead(i, 5) + lead(i, depth=i)
        s += keep(i, i, z=i) + keep()
        s += order(b=trace(log, 1), a=trace(log, 2)) + order(c=3, a=1, b=i)
        s += scale(i) + scale(i, by=3)
        if i % 1000 == 0:
            s += acc(i)
    return s + len(log) + log[0] * 10 + log[1]


def changed(n, fns):
    # The defaults change while the loop that calls with them runs.
    area = fns[0]
    s = 0
    for i in range(n):
        s += scale(i) + area(i)
        if i == n // 2:
            scale.__kwdefaults__['by'] = 7
            area.__kwdefaults__['depth'] = 3
            area.__defaults__ = (4,)
    scale.__kwdefaults__['by'] = 2
    return s


fns = make(1)
print(changed(N, make(1)))
print(hot(N, fns))
area = fns[0]
area.__defaults__ = (5,)
print(hot(3, fns))
scale.__kwdefaults__['by'] = 5      # in place: the function's version stays
print(hot(3, fns))
area.__kwdefaults__['depth'] = 7
print(hot(3, fns))
area.__kwdefaults__ = {'depth': 9}
print(hot(3, fns))
print(hot(3, make(2)))              # functions of the same code, other cells and defaults
print(hot(3, (lambda w, h=0, depth=0: w + h + depth,) + fns[1:]))
print(fns[4](0))                    # the default list made once

# Calls made out of line: a recursive callee and one too large to inline,
# with keyword-only and star parameters.
def rec(n, *, k=0):
    if n == 0:
        return k
    return rec(n - 1, k=k + 1)


def large(a, *rest, k=1, **kw):
    x = a
    for j in range(3):
        x = x * 3 + j
        x = x ^ (x >> 2)
        x = x - k
        x = x * 3 + j
        x = x ^ (x >> 2)
        x = x - k
        x = x * 3 + j
        x = x ^ (x >> 2)
        x = x - k
        x = x % 1000003
    for j in range(2):
        x = x + len(rest) * 7 + len(kw) * 11
        x = x * 5 % 1000033
        x = x + len(rest) * 7 + len(kw) * 11
        x = x * 5 % 1000033
    return x


class Box:
    def __init__(self, v):
        self.v = v

    def get(self, plus=0, *, times=1):
        return (self.v + plus) * times


def gen(n, *, step=1):
    for j in range(0, n, step):
        yield j


async def coro(x, *more, by=2):
    return x * by + len(more)


def cellparam(x, *, y=1):
    return (lambda: x + y)()


def posonly(a, /, **kw):
    return a + len(kw) * 10 + kw.get('a', 0) * 100


def run(c):
    try:
        c.send(None)
    except StopIteration as e:
        return e.value


def more(n):
    s = 0
    b = Box(3)
    for i in range(n):
        s += rec(3, k=1) + rec(2, k=i) + large(i, k=2) + large(i, 1, 2, k=3, q=4)
        s += b.get(plus=i) + b.get(1, times=2) + sum(gen(5, step=2)) + run(coro(i, 1, by=3))
        s += cellparam(i, y=2) + cellparam(x=i) + posonly(1, a=2) + posonly(i, b=i)
        add = lambda x, y=i, z=2, *, w=3: x + y * z + w      # made here, its defaults with it
        s += add(1) + add(1, w=0) + add(x=2, z=1)
    return s


print(more(N))
