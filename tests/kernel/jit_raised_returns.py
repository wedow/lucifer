# Exceptions that leave a function compiled code runs and are taken by a
# caller: a user function and kernel code as the raising root, two
# compiled callers in a row, a walker that
# reads or writes the caller's locals from inside the raising call (on
# the handler's way and on the leaving one), tools switched on from
# inside the raising call, values the raising frame held (their
# finalizers run, once), and AttributeError/NameError built in C. Each
# output line must be the interpreter's.
import sys
import traceback

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


def seen(j, n):
    return j < 3 or j >= n - 3


class Dies:
    deaths = 0

    def __del__(self):
        type(self).deaths += 1          # also at exit, after the module's names are gone


def g(d, k):
    keep = Dies()
    pair = [k, k]
    return d[pair[0]]


def a(d, k):
    return b(d, k) + 1


def b(d, k):
    return d[k]


def through(n):
    out = []
    d = {}
    for j in range(n):
        try:
            g(d, j)
        except KeyError as e:
            if seen(j, n):
                tb = e.__traceback__
                inner = tb.tb_next
                out.append(([f.name for f in traceback.extract_tb(tb)], tb.tb_lasti > 0, inner.tb_lineno,
                            inner.tb_frame.f_lineno, sorted(inner.tb_frame.f_locals), inner.tb_frame.f_back is None))
        try:
            a(d, j)
        except KeyError as e:
            if seen(j, n):
                out.append([f.name for f in traceback.extract_tb(e.__traceback__)])
    return out, Dies.deaths


class Peek:
    # Reads, then writes, the caller's locals from inside the failing lookup.
    def __init__(self, write):
        self.write = write

    def __hash__(self):
        f = sys._getframe(1)
        loc = f.f_locals
        if 's' in loc:
            v = loc['s']
            if self.write:
                loc['s'] = v + 1000
        return 7

    def __eq__(self, other):
        return False


def walked(n, write):
    d = {}
    s = 0
    for j in range(n):
        try:
            s += d[Peek(write) if j % 500 == 499 else j]
        except KeyError:
            s += 1
        s += j & 3
    return s


events = []


class Switch:
    def __hash__(self):
        sys.monitoring.use_tool_id(4, 'x')
        sys.monitoring.register_callback(4, sys.monitoring.events.RAISE, lambda c, o, e: events.append(c.co_name))
        sys.monitoring.set_events(4, sys.monitoring.events.RAISE)
        return 3

    def __eq__(self, other):
        return False


def switched(n):
    d = {}
    s = 0
    for j in range(n):
        try:
            s += d[Switch() if j == n - 10 else j]
        except KeyError:
            s += 1
    sys.monitoring.set_events(4, 0)
    sys.monitoring.free_tool_id(4)
    return s, events.count('switched')


class MyAE(AttributeError):
    def __init__(self, *args):
        super().__init__('my', *args)


def built(n):
    out = []
    for j in range(n):
        try:
            raise AttributeError('x%d' % (j & 1))
        except AttributeError as e:
            if seen(j, n):
                out.append((e.args, e.name, e.obj))
        try:
            raise AttributeError('y', name='n', obj=j & 1)
        except AttributeError as e:
            if seen(j, n):
                out.append((e.args, e.name, e.obj))
        try:
            raise MyAE(j & 1)
        except AttributeError as e:
            if seen(j, n):
                out.append((type(e).__name__, e.args))
        try:
            raise UnboundLocalError('u')
        except NameError as e:
            if seen(j, n):
                out.append((type(e).__name__, e.args, e.name))
    return out


print(through(N))
print(walked(N, False))
print(walked(N, True))
print(switched(N))
print(built(N))
d = {}
try:
    for j in range(N):
        a(d, j) if j == N - 1 else (j in d)
except KeyError as e:
    print('top', [f.name for f in traceback.extract_tb(e.__traceback__)])
