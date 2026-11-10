# Inlined frames run what may run Python: containment, formatting, slice
# stores, set and dict displays, functions made in them, generic subscript
# stores and iteration. Each helper is small enough to inline into its
# caller's loop, and some of what it meets runs a Python special method.


class Box:
    def __init__(self, v):
        self.v = v

    def __contains__(self, x):
        return x == self.v

    def __format__(self, spec):
        return f'<{self.v}:{spec}>'

    def __setitem__(self, k, v):
        self.v = (k, v)

    def __getitem__(self, k):
        return k

    def __iter__(self):
        return iter((self.v, self.v))

    def __hash__(self):
        return hash(self.v)

    def __eq__(self, o):
        return isinstance(o, Box) and o.v == self.v

    def __repr__(self):
        return f'Box({self.v!r})'


def has(c, x):
    return x in c


def fmt(i, b):
    return f'{i}-{b}-{b:>3}-{i!r}'


def splice(p, k):
    p[:k] = p[k - 1::-1]
    return p


def displays(i, b):
    return {i, b, i + 1}, {i: b, 'k': i}


def adder(k):
    f = lambda v: v + k             # noqa: E731
    return f(1) + f(2)


def store(d, k, v):
    d[k] = v
    return d


def unpack(seq):
    a, b = seq
    return a, b


def walk(it):
    s = 0
    for x in it:
        s += 1
    return s


def run(n):
    out = []
    b = Box(3)
    fs = frozenset([int, str])
    for i in range(n):
        r = (has(b, i % 5), has({1, 2}, i % 3), has(fs, type(i)), has((1, 2, 3), i % 4),
             fmt(i, b), splice(list(range(9)), 1 + i % 8), displays(i % 4, b), adder(i),
             store({}, i, b).get(i) is b, store(Box(0), i, 1).v, unpack(b), unpack([i, 2]),
             walk(b), walk(range(i % 3)))
        out.append(r)
    return out[-3:]


for row in run(3000):
    print(row)
try:
    has(5, 1)
except TypeError as e:
    print('TypeError:', e)
try:
    for i in range(3000):
        r = fmt(i, Box(i) if i < 2900 else object())
except TypeError as e:
    print('TypeError:', e)
print(r)
