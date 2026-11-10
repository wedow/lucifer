# Loops whose hot instruction meets a type its first compile guessed
# wrong, with no feedback to say otherwise: negation of floats and of
# Fractions where the tier speculates an int, also inside abs(). The guard
# fails until its site is marked generic; the unit built after the mark
# must not keep that guard and leave at it on every iteration.
# tools/jitcheck runs this at two sizes and requires that guard and side
# exits, and entries left to the interpreter, do not grow with the
# iterations.
import sys
from fractions import Fraction

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
xs = [float(i) * 0.5 for i in range(8)]
fs = [Fraction(i, 3) for i in range(8)]


def floats(n):
    s = 0.0
    for i in range(n):
        s += abs(-xs[i & 7])
    return s


def fractions(n):
    s = 0
    for i in range(n):
        s += -fs[i & 7]
    return s


def ints(n):
    s = 0
    for i in range(n):
        s += abs(-i)
    return s


print(floats(N), fractions(N), ints(N))
