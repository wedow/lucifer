# luc-1kz0: the loop of jit_rare_fails.py, whose iterable is a list on all
# but one call in 600 for the first 3,000 calls, and a tuple on every
# other call after that. The tuple, rare at first, becomes common: its
# guard's exits then cost what a build without the guard does, and the loop
# is built again to take both. tools/jitcheck runs this at two sizes and
# requires that exits do not grow with the iterations.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


def scale(x, k):
    return x * k + (x >> 1) - (k & 3)


def mix(a, b):
    return (a ^ b) + ((a & 15) << 2) - (b | 1)


def fold(a, b, c):
    return scale(a, b) - mix(b, c) + scale(c, a & 3) - mix(a, c)


def total(xs, k):
    s = 0
    for x in xs:
        s += fold(x, k, s & 255) + fold(k, x, 3) - fold(s & 15, k, x)
        s ^= fold(s & 63, x, k) - fold(x & 7, s & 31, k) + fold(k, k, x)
        s &= 0xFFFFF
    return s


def run(n):
    lst = list(range(20))
    tup = tuple(lst)
    s = 0
    for i in range(n):
        rare = i % 600 == 599 if i < 3000 else i % 2 == 1
        s += total(tup if rare else lst, i & 31)
    return s


print(run(N))
