# Calls of learned callees that compiled code cannot bind, or binds and
# then meets something else: errors raised by hot calls (too many or too
# few arguments, unknown and duplicate keywords, keys that are not strs),
# defaults replaced by a tuple of another size or by None, __code__
# replaced, keyword-only defaults in a dict subclass or a dict with a key
# that is not a str, and star forwarding through recursion. Each output
# line must be the interpreter's.
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


def rec(n, *rest, k=0, **kw):
    if n == 0:
        return k + len(rest) + len(kw)
    return rec(n - 1, *rest, k=k + 1, **kw)


def forwarded(n):
    s = 0
    for i in range(n):
        s += rec(3, i, k=1) + rec(2, k=i, z=1)
    return s


def resized(n):
    area = make(0)[0]
    s = 0
    for i in range(n):
        try:
            s += area(i, depth=2)
        except TypeError as e:
            s += len(str(e))
        if i == n // 3:
            area.__defaults__ = (1, 2)          # a longer tuple: w gets a default too
        if i == 2 * n // 3:
            area.__defaults__ = None            # none: area(i) needs h
    return s


def other(k):
    return lambda w, h=2, *, depth=1: w - h - depth - k


def recode(n):
    area = make(1)[0]
    s = 0
    for i in range(n):
        s += area(i, depth=2)
        if i == n // 2:
            area.__code__ = other(1).__code__
    return s


class KwSub(dict):
    pass


def odd_kwdefaults(n):
    def f(x, *, k=1):
        return x + k
    f.__kwdefaults__ = KwSub(k=5)
    g = make(0)[0]
    g.__kwdefaults__ = {'depth': 3, 1: 2}
    s = 0
    for i in range(n):
        s += f(i) + g(i)
        try:
            s += make(0)[5](a=1)
        except TypeError as e:
            s += len(str(e))
    return s


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
    # Hot, so that compiled calls meet the errors too.
    seen = []
    for _ in range(300):
        try:
            r = call()
        except TypeError as e:
            r = 'TypeError ' + str(e)
        if r not in seen:
            seen.append(r)
    print(seen)
print(d)


print(forwarded(N))
print(resized(N))
print(recode(N))
print(odd_kwdefaults(N))
