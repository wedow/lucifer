# Hot code holding instructions the optimizing tier compiles as calls into
# the runtime or leaves to the interpreter for: closures made in a loop,
# cells a loop rebinds and a closure reads, *args forwarding, rich
# comparison and instance creation through the kernel's generic paths,
# a nonlocal rebound by a callee, and comprehensions, whose loops are
# their function's and whose results are built by LIST_APPEND, SET_ADD and
# MAP_ADD (and a tuple display from a starred list); any() of a generator,
# which loads the builtin as a common constant; slices read and assigned;
# a list unpacked; and calls with keywords and star arguments. Each result
# must be the interpreter's; tools/jitcheck also runs this at two sizes and
# requires that the exits compiled code takes do not grow with the
# iterations.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


def closures(n):
    s = 0
    for i in range(n):
        f = lambda: i
        s += f()
    return s


def nested(n):
    s = 0
    for i in range(n):
        for j in range(2):
            s += j

        def g():
            nonlocal s
            s += i

        g()
    return s


def star(f, *args):
    return f(*args)


def add(a, b):
    return a + b


def forwarding(n):
    s = 0
    for i in range(n):
        s += star(add, i, 1)
    return s


class V:
    def __init__(self, x):
        self.x = x

    def __lt__(self, other):
        return self.x < other.x


def richcmp(n):
    a, b = V(1), V(2)
    c = 0
    for i in range(n):
        if a < b:
            c += 1
    return c


def instances(n):
    s = 0
    for i in range(n):
        s += V(i).x
    return s


def cells(n):
    t = 0

    def bump():
        nonlocal t
        t += 1

    for i in range(n):
        bump()
    return t


def comprehensions(n):
    s = 0
    t = (1, 2)
    for i in range(n):
        s += len([j for j in t if j]) + len({j for j in t}) + len({j: i for j in t}) + len((*t, i))
    return s


def sequences(n):
    s = 0
    t = 'abcdef'
    items = [0, 1, 2]
    for i in range(n):
        s += any(c == 'e' for c in t[i & 3:])
        items[0:1] = [i]
        a, b, c = items
        s += len(t[1:3]) + a + c
    return s


def keyed(a, b=0, *rest, **kw):
    return a + b + len(rest) + len(kw)


def keywords(n):
    s = 0
    t = (1, 2, 3)
    kw = {'z': 1}
    for i in range(n):
        if i & 1:
            s += keyed(i, b=2) + keyed(*t, **kw) + keyed(*t)
    return s


print(closures(N), nested(N), forwarding(N), richcmp(N), instances(N), cells(N), comprehensions(N), sequences(N), keywords(N))
