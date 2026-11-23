# Instructions whose failure is a bit, not an object (a del of a missing
# key, a store of an attribute a __slots__ class lacks, a store with an
# unhashable key), failing in a hot loop under a handler of the same
# function. tools/jitcheck runs this at two sizes and requires that the
# compiles do not grow and that hot() is never retired: an exit that no
# rebuild can take in the unit must not make it rebuild or give up.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class Slotted:
    __slots__ = ('x',)


def hot(n):
    d = {}
    o = Slotted()
    s = 0
    for j in range(n):
        try:
            del d[j]
        except KeyError:
            s += 1
        try:
            o.y = j
        except AttributeError:
            s += 2
        try:
            d[[j]] = j
        except TypeError:
            s += 3
    return s


print(hot(N))
