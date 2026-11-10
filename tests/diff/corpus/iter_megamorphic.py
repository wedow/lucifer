"""Kernel loops (str.join, set(), list(), tuple(), sum) over iterators of
many classes, so that their steps go generic and then take the classes they
meet most in line: Python iterators that end with StopIteration or a
subclass of it, raise another error, change their class's __next__ while a
loop runs, or have a __next__ that is not a plain function; generators,
deques, maps and filters."""
from collections import deque
from support import loops


class Count:
    def __init__(self, n):
        self.n = n

    def __iter__(self):
        return self

    def __next__(self):
        if self.n <= 0:
            raise StopIteration
        self.n -= 1
        return str(self.n)


class MyStop(StopIteration):
    pass


class EndsSubclass(Count):
    def __next__(self):
        if self.n <= 0:
            raise MyStop('done')
        self.n -= 1
        return 'e'


class Fails(Count):
    def __next__(self):
        if self.n <= 0:
            raise ValueError('broken iterator')
        self.n -= 1
        return 'f'


class Mutating(Count):
    def __next__(self):
        if self.n <= 0:
            raise StopIteration
        self.n -= 1
        if self.n == 2:
            Mutating.__next__ = Mutating._later
        return 'm'

    def _later(self):
        if self.n <= 0:
            raise StopIteration
        self.n -= 1
        return 'L'


class Static:
    def __init__(self, n):
        self.n = n

    def __iter__(self):
        return self

    @staticmethod
    def __next__():
        raise StopIteration


class Bound:
    def __init__(self, n):
        self.items = iter(['b'] * n)

    def __iter__(self):
        return self

    def __next__(self):
        return next(self.items)


def gen(n):
    for i in range(n):
        yield str(i)


MAKERS = [Count, EndsSubclass, gen, lambda n: deque(str(i) for i in range(n)), lambda n: map(str, range(n)),
          lambda n: filter(None, [str(i) for i in range(n)]), Bound, lambda n: iter([str(i) for i in range(n)])]


def consume(make, n):
    out = []
    out.append(''.join(make(n)))
    out.append(sorted(set(make(n))))
    out.append(list(make(n)))
    out.append(tuple(make(n)))
    return out


def run():
    out = []
    for _ in range(40):
        for make in MAKERS:
            out = consume(make, 6)
    for _ in range(300):
        out = consume(Count, 7)
    results = [consume(make, 5) for make in MAKERS]
    try:
        ''.join(Fails(3))
    except ValueError as e:
        results.append(('join', str(e)))
    try:
        set(Fails(2))
    except ValueError as e:
        results.append(('set', str(e)))
    saved = Mutating.__dict__['__next__']
    results.append(''.join(Mutating(6)))
    Mutating.__next__ = saved
    results.append(list(Static(3)))
    results.append(out)
    return results


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
