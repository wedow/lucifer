# Where RecursionError is raised, by the frames an f_back walk counts at the
# deepest level, for recursion through each way a call enters a function:
# a plain call, a method, C calling back (sorted's key, __hash__), nested
# generators, and two functions a caller can run in line. The same with the
# limit raised in the middle of the descent, the deep repr of C containers,
# and the traceback's last entries.
import sys

deepest = 0


def frames():
    n, f = 0, sys._getframe(1)
    while f is not None:
        n, f = n + 1, f.f_back
    return n


def note():
    global deepest
    deepest = frames()


def plain(n):
    note()
    return plain(n + 1)


class M:
    def m(self, n):
        note()
        return self.m(n + 1)


def key(x):
    note()
    return sorted([x + 1], key=key)[0]


class H:
    def __init__(self, n):
        self.n = n

    def __hash__(self):
        note()
        return hash(H(self.n + 1))


def gen(n):
    note()
    yield from gen(n + 1)


def ping(n):
    note()
    return pong(n + 1)


def pong(n):
    return ping(n + 1)


KINDS = {
    'plain': lambda: plain(0),
    'method': lambda: M().m(0),
    'sorted key': lambda: key(0),
    '__hash__': lambda: hash(H(0)),
    'generators': lambda: next(gen(0)),
    'inlined pair': lambda: ping(0),
}


def reach(run, room):
    global deepest
    deepest = 0
    old = sys.getrecursionlimit()
    sys.setrecursionlimit(frames() + room)
    try:
        run()
    except RecursionError as e:
        tb = e.__traceback__
        while tb.tb_next is not None:
            tb = tb.tb_next
        last = tb.tb_frame.f_code.co_name
    else:
        last = None
    finally:
        sys.setrecursionlimit(old)
    return deepest, last


for _ in range(200):
    for run in KINDS.values():
        reach(run, 20)
for name, run in KINDS.items():
    print(name, [reach(run, room) for room in (20, 21, 60)])


# The limit raised in the middle of a descent: the deeper limit holds.
def raising(n, at, extra):
    note()
    if n == at:
        sys.setrecursionlimit(sys.getrecursionlimit() + extra)
    return raising(n + 1, at, extra)


for _ in range(50):
    reach(lambda: raising(0, 5, 10), 30)
print('raised mid-descent', reach(lambda: raising(0, 10, 25), 30))

# The repr of a container nested deeper than any stack.
x = []
for _ in range(200000):
    x = [x]
try:
    repr(x)
    print('repr ok')
except RecursionError as e:
    print('repr', type(e).__name__, str(e).split(' (')[0])
del x
