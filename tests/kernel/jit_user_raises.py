# Exceptions raised in a hot loop of user code and dropped by a handler of
# the same function: a dict miss, a list index, getattr of a missing
# attribute, next() on an exhausted iterator, raise of a class, and an
# exception from a call not made in line. tools/jitcheck runs this at two
# sizes and requires that compiled code does not leave per iteration (its
# error and side exits): each handler runs in compiled code.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class Oops(Exception):
    pass


class Box:
    x = 1


def far(j):
    # Large enough to stay a call of its own.
    t = 0
    for k in range(3):
        t += k * j
        t ^= k
        t += j % 7
    if j & 3 == 0:
        raise Oops(j, t)
    return t


def dropped(n):
    d = {1: 2}
    xs = [1, 2, 3]
    it = iter(())
    b = Box()
    s = 0
    for j in range(n):
        try:
            s += d[j & 1]
        except KeyError:
            s -= 1
        try:
            s += xs[j % 5]
        except IndexError:
            s -= 2
        try:
            s += getattr(b, 'y' if j & 1 else 'x')
        except AttributeError:
            s -= 3
        try:
            s += next(it)
        except StopIteration:
            s += 4
        try:
            if j & 1:
                raise Oops
            s += 1
        except Oops:
            s -= 5
        try:
            s += far(j)
        except Oops:
            s -= 6
    return s


print(dropped(N))
