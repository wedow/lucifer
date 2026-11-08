# Hot loops that unpack what is not an exact tuple or list: named tuples
# (tokenize's TokenInfo is one), a tuple subclass with its own __iter__,
# a generator, and, now and then, a sequence of the wrong length. Each
# result must be the interpreter's; tools/jitcheck also runs this at two
# sizes and requires that the side exits compiled code takes do not grow
# with the iterations: the unpack is the kernel's, called from compiled
# code.
import sys
from collections import namedtuple

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000

Tok = namedtuple('Tok', 'type text start end')


class Reversed(tuple):
    def __iter__(self):
        return iter(tuple(reversed(self)))


def pairs():
    yield 1
    yield 2


def run(n):
    toks = [Tok(i % 5, str(i), (i, 0), (i, 1)) for i in range(7)]
    total = 0
    errors = []
    for i in range(n):
        typ, text, start, end = toks[i % 7]
        total += typ + len(text) + start[0] - end[1]
        a, b = Reversed((i, 1))
        total += a - b
        c, d = pairs()
        total += c * d
        if i % 997 == 0:
            try:
                x, y = toks[0]
            except ValueError as e:
                errors.append(str(e))
    return total, errors[:2], len(errors)


print(run(N))
