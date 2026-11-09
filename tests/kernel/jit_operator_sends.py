# Operators as sends: a site's operand classes pick the special method in
# line, per pair, with the kernel's dispatch folded on them. Each case runs
# hot enough to compile, then changes what the site meets.
import math


class V:
    def __init__(self, x):
        self.x = x

    def __add__(self, o):
        if isinstance(o, V):
            return V(self.x + o.x)
        if isinstance(o, (int, float)):
            return V(self.x + o)
        return NotImplemented

    def __radd__(self, o):
        return V(o + self.x)

    def __mul__(self, k):
        return V(self.x * k)

    def __neg__(self):
        return V(-self.x)

    def __lt__(self, o):
        return self.x < o.x

    def __eq__(self, o):
        return isinstance(o, V) and self.x == o.x

    __hash__ = None

    def __repr__(self):
        return f'V({self.x!r})'


class W(V):
    def __radd__(self, o):          # a subclass's reflection goes first
        return W(-1000)


class N:
    def __add__(self, o):
        return NotImplemented


def add(a, b):
    return a + b


def mixed(n):
    s = 0
    for i in range(n):
        s += i if i % 3 else i * 0.5
    return s


def run_add(pairs, reps):
    out = []
    for _ in range(reps):
        for a, b in pairs:
            try:
                out.append(repr(add(a, b)))
            except TypeError as e:
                out.append('TypeError: ' + str(e))
    return out[-len(pairs):]


print(run_add([(V(1), V(2))], 3000))
print(run_add([(V(1), V(2)), (V(1), 2.5), (3, V(4))], 3000))
print(run_add([(V(1), W(2)), (1, 2), (1.5, 2), ('a', 'b')], 3000))
print(run_add([(V(1), N()), (N(), N()), ([1], (2,)), (V(1), V(2))], 3000))
print(run_add([(V(1), V(2)), ((1,), (2,)), (V(0), 1), (1, V(0)), (2.0, 3.0), (W(1), W(2))], 3000))
print(mixed(5000))


def inplace(n):
    acc = V(0)
    for i in range(n):
        acc += V(i)
        acc *= 1
    return acc


print(inplace(3000))


def powers(n):
    s = 0.0
    t = 0
    for i in range(1, n):
        s += (i * 0.5) ** -1.5 + (i * 0.25) ** 2 + (i + 0.5) ** 0.5
        t += i ** 2
    return s, t


print(powers(3000))
for v, e in [(0.0, -1.0), (-8.0, 1 / 3), (1e300, 2), (-2.0, 3), (2.0, -1074), (10.0, 400.0)]:
    def p(v, e):
        try:
            return repr(v ** e)
        except (ZeroDivisionError, OverflowError) as ex:
            return type(ex).__name__ + ': ' + str(ex)
    r = None
    for _ in range(2000):
        r = p(v, e)
    print(r)


def compares(items, reps):
    c = 0
    for _ in range(reps):
        for a, b in items:
            c += a < b
            c += a == b
    return c


print(compares([(V(1), V(2))], 3000))
print(compares([(V(1), V(2)), (1, 2.5), (2.5, 1)], 3000))
try:
    compares([(V(1), 3)], 2000)
except AttributeError as e:
    print('AttributeError:', e)
try:
    compares([(object(), object())], 2000)
except TypeError as e:
    print('TypeError:', e)


def negs(items, reps):
    out = None
    for _ in range(reps):
        out = [repr(-x) for x in items]
    return out


print(negs([1.5], 3000), negs([1.5, 2, V(3)], 3000), negs([2 ** 70, -1.0], 3000))
print(negs([math.inf, True], 3000))
try:
    negs(['s'], 2000)
except TypeError as e:
    print('TypeError:', e)


def invs(items, reps):
    out = None
    for _ in range(reps):
        out = [~x for x in items]
    return out


print(invs([1, True, 2 ** 70], 3000))


# A class whose method changes while compiled code sends to it.
class M:
    def __add__(self, o):
        return 1


def madd(n):
    s = 0
    m = M()
    for i in range(n):
        s += m + m
        if i == n // 2:
            M.__add__ = lambda self, o: 2
    return s


print(madd(6000))


# More classes than a site's ways: the generic dispatch.
classes = [type(f'C{i}', (), {'__add__': (lambda i: lambda self, o: i)(i)}) for i in range(8)]
objs = [k() for k in classes]


def poly(n):
    s = 0
    for i in range(n):
        o = objs[i % 8]
        s += o + o
    return s


print(poly(8000))
