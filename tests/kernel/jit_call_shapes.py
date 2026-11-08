# Calls of compiled functions that do not inline (each loops), in the
# shapes real programs make: star and keyword parameters, keyword-only
# parameters with defaults, parameters a closure captures (cells), and
# calls with keywords, of functions and bound methods. tools/jitcheck runs
# this at two sizes and requires that no call per iteration enters compiled
# code through the interpreter (entries) or from C's frame entry (direct
# calls): each call takes its callee's direct entry.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


def star(a, *rest):
    s = a
    for x in rest:
        s += x
    return s


def stars(a, *rest, **kw):
    s = a + len(kw)
    for x in rest:
        s += x
    for k in kw:
        s += kw[k]
    return s


def kwonly(a, *, k=3, j=4):
    s = 0
    for x in (a, k, j):
        s += x
    return s


def captured(a, b=1):
    def get():
        return a + b
    s = 0
    for _ in range(2):
        s += get()
    return s


def plain(a, b, c=5):
    s = 0
    for x in (a, b, c):
        s += x
    return s


class K:
    def method(self, a, *, k=1):
        s = 0
        for x in (a, k):
            s += x
        return s


def hot(fs, n):
    st, ss, ko, cp, pl, m = fs
    s = 0
    for i in range(n):
        s += st(i) + st(i, 1, 2)
        s += ss(i) + ss(i, 1, x=2, y=3)
        s += ko(i) + ko(i, k=5) + ko(i, j=1, k=2)
        s += cp(i) + cp(i, 2)
        s += pl(i, b=2) + pl(c=1, b=2, a=i) + pl(i, 1, c=0)
        s += m(i) + m(i, k=7)
    return s


fs = (star, stars, kwonly, captured, plain, K().method)
print(hot(fs, N))
# What the calls bind changes under them: defaults, keyword-only defaults.
kwonly.__kwdefaults__ = {'k': 30, 'j': 40}
plain.__defaults__ = (50,)
print(hot(fs, 3))
kwonly.__kwdefaults__ = {'j': 1}
for call in (lambda: kwonly(1), lambda: plain(1, c=2), lambda: plain(1, 2, b=3), lambda: plain(1, 2, d=3),
             lambda: star(a=1, b=2), lambda: K().method(1, 2)):
    try:
        print(call())
    except TypeError as e:
        print('TypeError', e)
