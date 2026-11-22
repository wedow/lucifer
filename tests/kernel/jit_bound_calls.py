# Calls whose callee compiled code learns from the site, not from a
# constant: a nested function, a closure, a function passed in or held in a
# local, called with keywords, defaults, keyword-only parameters and star
# arguments on either side. tools/jitcheck runs this at two sizes and
# requires that no call per iteration is made as C makes a keyword or star
# call: each binds its arguments at compile time and goes on in line.
# After the loops, the callees' defaults change (replaced, and changed in
# place) and the callee is replaced under the compiled calls, and calls
# that cannot bind raise the interpreter's errors.
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

area, fwd, lead, keep, acc, order = make(3)
d = {'depth': 2}
calls = [
    lambda: area(),
    lambda: area(1, 2, 3),
    lambda: area(1, w=2),
    lambda: area(1, x=2),
    lambda: area(1, h=2, h2=3),
    lambda: fwd(1, 2, 3),
    lambda: fwd(1, **{'w': 2}),
    lambda: fwd(1, **{1: 2}),
    lambda: fwd(**{'w': 1}, **{'w': 2}),
    lambda: lead(),
    lambda: lead(1, **d, depth=3),
    lambda: order(1),
    lambda: order(a=1),
    lambda: order(a=1, b=2, d=4),
    lambda: keep(*[1, 2], **d),
]
for call in calls:
    try:
        print(call())
    except TypeError as e:
        print('TypeError', e)
print(d)
