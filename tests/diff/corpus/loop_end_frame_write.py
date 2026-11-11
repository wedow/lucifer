"""A for loop over iterators that the loop steps through a call (several
classes at one site), where the last __next__, the one that ends the loop,
writes the looping frame's local through f_locals or keeps a frame object
of it: after the loop the frame sees the value written, and its later
locals stay its own. Then the same loop in a small function called from a
hot loop, where the frames the step's call ran under are kept."""
import sys
from support import loops


class Ends:
    def __init__(self, n, tag):
        self.n = n
        self.tag = tag

    def __iter__(self):
        return self

    def __next__(self):
        if self.n <= 0:
            sys._getframe(1).f_locals['seen'] = self.tag
            raise StopIteration
        self.n -= 1
        return self.n


class Keeps(Ends):
    def __next__(self):
        if self.n <= 0:
            Keeps.kept = sys._getframe(1)
            raise StopIteration
        self.n -= 1
        return self.n


class Plain(Ends):
    def __next__(self):
        if self.n <= 0:
            raise StopIteration
        self.n -= 1
        return self.n


def walk(it):
    seen = 'none'
    total = 0
    for v in it:
        total += v
    after = total * 2
    return seen, total, after


def outer(n):
    out = []
    for i in range(n):
        out.append(walk([Ends, Keeps, Plain][i % 3](i % 5, 'end%d' % i)))
    return out


def run():
    rows = outer(60)
    kept = Keeps.kept
    return rows[-6:], kept.f_code.co_name, sorted(k for k in kept.f_locals)


Keeps.kept = None
for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
