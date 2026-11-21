# luc-tax0: a hot loop whose iterable is a list on all but one call in 600,
# and a tuple then. Its guard fails now and then for as long as the program
# runs. A unit built again without the list's in-line iteration costs a
# build, and a helper call per item from then on, which the rare exits do
# not: the loop keeps it. tools/jitcheck runs this at two sizes and requires
# that the helper calls compiled code makes do not grow with the iterations.
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
        s += total(tup if i % 600 == 599 else lst, i & 31)
    return s


print(run(N))
