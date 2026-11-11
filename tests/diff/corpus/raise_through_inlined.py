"""Exceptions raised in small functions that inline into their callers and
caught further out: a user iterator's StopIteration ending a loop, a
ValueError from two frames in, caught where the traceback is read (its
frames and lines), and a frame object kept from the traceback, whose locals
outlive the call."""
import traceback
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
        return self.n


def check(x):
    if x > 6:
        raise ValueError('too big: %d' % x)
    return x


def middle(x):
    y = x * 2
    return check(y) + 1


def outer(n):
    total = 0
    for i in range(n):
        try:
            total += middle(i)
        except ValueError as e:
            tb = traceback.extract_tb(e.__traceback__)
            frame = e.__traceback__.tb_next.tb_frame
            return total, [(t.name, t.line) for t in tb], sorted(frame.f_locals.items())
    return total, [], []


def run():
    out = []
    for _ in range(200):
        out = [sum(Count(5)), list(Count(3)), ''.join(str(v) for v in Count(4)), outer(10)]
    return out


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
