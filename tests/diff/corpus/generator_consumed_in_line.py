"""generator consumed in line: sum, set, tuple and join over generator
expressions whose bodies release objects with finalizers that collect, raise
midway, or were partly consumed first."""
import gc
from support import loops

seen = []


class Noisy:
    def __init__(self, v):
        self.v = v

    def __del__(self):
        seen.append(self.v)
        if self.v % 7 == 0:
            gc.collect()


def run():
    xs = list(range(20))
    floats = [i * 0.5 for i in range(20)]
    a = sum(Noisy(v).v for v in xs)
    b = sum(v * v for v in floats)
    c = sum(v for v in [1, 2, 2.5, 3])
    d = sorted(set(v % 5 for v in xs))
    e = tuple(v + 1 for v in xs[:6])
    f = ','.join(str(v) for v in xs[:5])
    g = (v for v in xs)
    first = next(g), next(g)
    h = sum(g)
    try:
        sum(1 // (v - 3) for v in xs)
    except ZeroDivisionError as exc:
        i = type(exc).__name__
    del seen[:]
    return a, b, c, d, e, f, first, h, i


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
