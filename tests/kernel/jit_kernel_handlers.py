# Exceptions a call raises into a handler of the runtime's own code, hot:
# a sequence iterated by __getitem__ until IndexError, an iterator whose
# __next__ is not inlined, next() with a default, getattr() with a default
# and hasattr() over __getattr__ that raises, `in` over an object that is
# not iterable; and the same calls raising what the handler does not take,
# whose traceback and context must be the interpreter's.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class Seq:
    def __init__(self, n):
        self.n = n

    def __getitem__(self, i):
        if i >= self.n:
            raise IndexError(i)
        return i * 2


class Far:
    """An iterator whose __next__ is too big to take in line."""

    def __init__(self, n):
        self.n = n

    def __iter__(self):
        return self

    def __next__(self):
        n = self.n
        if n <= 0:
            raise StopIteration(n)
        a = b = c = d = e = f = g = h = n
        for _ in range(3):
            a, b, c, d, e, f, g, h = b + 1, c + a, d - b, e * 1, f + c, g - d, h + e, a + f
        self.n = n - 1
        return (a + b + c + d + e + f + g + h) % 7


class Lazy:
    def __getattr__(self, name):
        if name.startswith('x'):
            return len(name)
        raise AttributeError(name)


class Bad:
    def __next__(self):
        raise ValueError('bad')

    def __iter__(self):
        return self


def gen(n):
    yield from range(n)


def sequences(n):
    s = 0
    for i in range(n):
        for x in Seq(i % 6):
            s += x
        for x in Far(i % 3):
            s += x
        s += next(iter(()), -1) + next(gen(i % 2), -2)
    return s


def attributes(n):
    s = 0
    lazy = Lazy()
    for i in range(n):
        s += getattr(lazy, 'xx', 0) + getattr(lazy, 'yy', 5) + hasattr(lazy, 'zz') + hasattr(lazy, 'x')
    return s


def contains(n):
    s = 0
    for i in range(n):
        try:
            s += 3 in Seq(5)
        except TypeError:
            s += 100
    return s


def unmatched(n):
    seen = set()
    for i in range(n):
        try:
            for x in Bad():
                pass
        except ValueError as e:
            tb, d = e.__traceback__, 0
            while tb is not None:
                d += 1
                tb = tb.tb_next
            seen.add((str(e), d, e.__context__ is None))
        try:
            next(Bad())
        except ValueError as e:
            seen.add(('next', e.__context__ is None))
    return sorted(seen, key=str), sys.exception()


def in_handler(n):
    out = set()
    for i in range(n):
        try:
            raise KeyError(i)
        except KeyError:
            s = 0
            for x in Seq(3):
                s += x
            out.add((s, type(sys.exception()).__name__))
    return sorted(out), sys.exception()


print(sequences(N))
print(attributes(N))
print(contains(N))
print(unmatched(N))
print(in_handler(N))
