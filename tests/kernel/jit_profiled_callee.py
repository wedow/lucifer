# A call whose callable is a value (a parameter, a local): the site has
# met one function, and compiled code calls that one in line. A builtin
# method bound to its object (w = d.append), a bound Python method, a
# plain function; a bound method that is a temporary and dies during its
# own call; the site meeting another callable late, another object of the
# same method, a method of another type, and something not callable.
import collections
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class Acc:
    def __init__(self):
        self.total = 0
        self.seen = []

    def add(self, x):
        self.total += x
        return self.total

    def note(self, x, y=7):
        self.seen.append((x, y))


def feed(w, n):
    for i in range(n):
        w(i)


def feed_result(w, n):
    out = 0
    for i in range(n):
        r = w(i)
        if r is not None:
            out += r
    return out


def builtin_methods(n):
    d = collections.deque()
    l = []
    s = set()
    feed(d.append, n)
    feed(l.append, n)
    feed(s.add, n)
    feed(d.appendleft, 5)
    m = {}
    got = feed_result(m.get, n)
    return len(d), d[0], d[-1], len(l), l[-1], len(s), got


def python_methods(n):
    a = Acc()
    b = Acc()
    t = feed_result(a.add, n)
    t += feed_result(b.add, n // 2)
    feed(a.note, 3)
    return t, a.total, b.total, a.seen


def temporary(n):
    # The bound method is on the stack only: it dies when its call's
    # operands go, and its object must outlive the call.
    out = 0
    for i in range(n):
        out += Acc().add(i)
        out += (lambda: Acc().add)()(1)
        collections.deque(maxlen=2).append(i)
    return out


def changes(n):
    d = collections.deque()
    l = []
    a = Acc()
    ways = [d.append] * (n - 6) + [l.append, a.add, d.appendleft, print.__call__, len, d.append]
    out = []
    for i, w in enumerate(ways):
        r = w('xy') if w is len else w(i)
        if i >= n - 8:
            out.append(r)
    return len(d), len(l), a.total, out


def raises(n):
    d = collections.deque()
    w = d.append
    bad = 0
    for i in range(n):
        try:
            if i == n - 2:
                w = None
            if i == n - 1:
                w = d.pop
            w(i)
        except TypeError as e:
            bad += 1
    return bad, len(d)


def identity(n):
    a = Acc()
    m = a.add
    k = 0
    for i in range(n):
        k += m.__self__ is a
        m(1)
    return k, a.total


for f in (builtin_methods, python_methods, temporary, changes, raises, identity):
    print(f.__name__, f(N))
    print(f.__name__, f(N // 6))
