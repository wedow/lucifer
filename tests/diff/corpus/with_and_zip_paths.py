"""with statements over context managers whose __enter__/__exit__ are
functions, kernel methods (memoryview, a lock), a classmethod, a callable
object and missing methods (the errors), an exception the __exit__ swallows;
and zip of two iterables, a subclass, a non-iterable and strict, in loops."""
import threading
from support import loops


class Plain:
    def __init__(self):
        self.log = []

    def __enter__(self):
        self.log.append('in')
        return self

    def __exit__(self, *exc):
        self.log.append(exc[0].__name__ if exc[0] else 'out')
        return exc[0] is KeyError


class Odd:
    @classmethod
    def __enter__(cls):
        return cls.__name__

    __exit__ = staticmethod(lambda *a: None)


class NoExit:
    def __enter__(self):
        return 1


class Z(zip):
    pass


def run():
    out = []
    p = Plain()
    lock = threading.Lock()
    for i in range(200):
        with p:
            if i == 3:
                raise KeyError(i)
        with lock, memoryview(b'abc') as v:
            out.append(v.nbytes)
        with Odd() as name:
            out.append(name)
    out = out[:6] + [p.log[:10], len(p.log)]
    for bad in (NoExit(), 3):
        try:
            with bad:
                pass
        except TypeError as e:
            out.append(str(e))
    total = 0
    for i in range(300):
        for a, b in zip(range(i % 7), 'abcdefg'):
            total += a
    out.append(total)
    out.append(list(Z('ab', 'xyz')))
    try:
        zip('ab', 5)
    except TypeError as e:
        out.append(str(e))
    try:
        list(zip('ab', 'c', strict=True))
    except ValueError as e:
        out.append(str(e))
    return out


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
