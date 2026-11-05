# Hot code holding instructions the optimizing tier leaves to the
# interpreter for: closures made in a loop, *args forwarding, rich
# comparison and instance creation through the kernel's generic paths,
# and a nonlocal rebound by a callee. Each result must be the
# interpreter's; tools/jitcheck also runs this at two sizes and requires
# that the exits compiled code takes do not grow with the iterations.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


def closures(n):
    s = 0
    for i in range(n):
        f = lambda: i
        s += f()
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


print(closures(N), forwarding(N), richcmp(N), instances(N), cells(N))
