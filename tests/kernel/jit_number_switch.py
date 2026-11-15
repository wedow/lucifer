# Operators whose operands are ints at times and floats at others: a sum
# that starts as an int and turns float, a function given either, both
# operands varying, comparisons, in-place forms; and a third kind turning
# up later (a bool, a big int, a Fraction-like object, a string).
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


def partial_sum(n):
    total = 0
    for i in range(n):
        total += 1.0 / (i + 1)
    return total


def scale(x, k):
    return x * k + 1


def either(n):
    acc = 0.0
    hits = 0
    for i in range(n):
        v = i if i % 3 else i * 0.5
        w = 2 if i % 5 else 2.5
        acc += scale(v, w)
        acc -= v / (w + 1)
        if v < w:
            hits += 1
        if w <= v * 2:
            hits += 2
        if v == i:
            hits += 4
    return acc, hits


def ints_stay_ints(n):
    out = []
    for i in range(n):
        v = i if i % 2 else float(i)
        r = v + 1
        s = v * 2
        t = v - i
        if i >= n - 6:
            out.append((r, s, t, type(r).__name__, type(s).__name__, type(t).__name__))
    return out


def overflow(n):
    x = 1
    out = 0.0
    for i in range(n):
        v = x if i % 2 else 0.5
        out += v * 3
        x = x * 3 if x < 10 ** 30 else 1
    return out


class Num:
    def __init__(self, v):
        self.v = v

    def __add__(self, other):
        return ('add', self.v, other)

    def __radd__(self, other):
        return ('radd', other, self.v)

    def __lt__(self, other):
        return 'lt'


def third_kind(n):
    out = []
    vals = [1, 2.5] * (n // 2) + [True, 2 ** 70, Num(3), 'x', None]
    for i, v in enumerate(vals):
        try:
            r = v + 1
            s = 1.5 + v
            c = v < 2
        except TypeError as e:
            r = s = c = type(e).__name__
        if i >= len(vals) - 7:
            out.append((r, s, c))
    return out


def unbound(n):
    out = []
    for i in range(n):
        if i % 2:
            y = 1.5
        elif i > 4:
            y = 2
        try:
            out.append(y + i)
        except UnboundLocalError:
            out.append('unbound')
    return out[:8], sum(x for x in out if not isinstance(x, str))


def nan_and_zero(n):
    out = []
    for i in range(n):
        v = float('nan') if i % 7 == 0 else (0 if i % 2 else -0.0)
        out.append((v == v, v < 1, v != 0, repr(v + 0)))
    return out[-7:], len(set(out))


for f in (partial_sum, either, ints_stay_ints, overflow, third_kind, unbound, nan_and_zero):
    print(f.__name__, f(N))
    print(f.__name__, f(N // 7))
