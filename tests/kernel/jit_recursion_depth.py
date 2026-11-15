# Recursion through inlined frames reaches the interpreter's depth, frame for
# frame: compiled code counts an inlined frame toward the limit wherever the
# count can be seen, at the calls it makes out of line, at its exits, and in a
# finalizer that a release inside an inlined frame runs.
import sys

deepest = 0


def outer(n):
    global deepest
    deepest = n
    return inner(n + 1)


def inner(n):
    return outer(n + 1)


def frames():
    n, f = 0, sys._getframe(1)
    while f is not None:
        n, f = n + 1, f.f_back
    return n


def reach(room):
    """How deep the chain gets with `room` frames left under the limit."""
    old = sys.getrecursionlimit()
    sys.setrecursionlimit(frames() + room)
    try:
        outer(0)
    except RecursionError:
        pass
    finally:
        sys.setrecursionlimit(old)
    return deepest


for i in range(300):
    reach(20)
for room in (20, 21, 50, 101):
    print('reach', room, reach(room))


# A finalizer run by a release inside an inlined frame sees that frame counted.
dug = 0


def dig(n):
    global dug
    dug = n
    dig(n + 1)


class Dying:
    def __del__(self):
        try:
            dig(0)
        except RecursionError:
            pass


def drop(x):
    x = None
    return x


def holder():
    return drop(Dying())


old = sys.getrecursionlimit()
sys.setrecursionlimit(frames() + 40)
for i in range(300):
    holder()
sys.setrecursionlimit(old)
print('finalizer', dug)
