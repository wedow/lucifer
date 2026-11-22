# Values compiled code keeps in registers across a release or a truth test
# whose C runs Python: raw ints and floats, objects, a loop's counters and
# sums, all live after the call. A finalizer (the last reference to a local
# dropped) and __bool__ (an `if` on an object) read the caller's locals on
# one late iteration, or write one through f_locals (PEP 667), which the
# function must see from then on; the other iterations run no probe, and
# every value must come out as the interpreter's.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 300

armed = []


def probe(frame):
    if armed:
        armed.pop()(frame)


def reader(frame):
    seen = sorted((k, v) for k, v in frame.f_locals.items() if isinstance(v, (int, float, str)))
    print('  read', frame.f_code.co_name, seen)


def writer(frame):
    frame.f_locals['x'] = 100


class Probe:
    def __del__(self):
        probe(sys._getframe(1))


class Truthy:
    def __bool__(self):
        probe(sys._getframe(1))
        return True


def release(n, late, act):
    x = 3
    s = 0
    f = 0.5
    g = 1.25
    name = 'k'
    xs = [1, 2, 3]
    for i in range(n):
        p = Probe()
        if i == late:
            armed.append(act)
        p = None
        s += x + xs[i % 3]
        f = f * 0.5 + g
        g += 1.0
    return s, x, f, g, name, xs


def truth(n, late, act):
    x = 3
    s = 0
    f = 0.5
    g = 1.25
    name = 'k'
    t = Truthy()
    for i in range(n):
        if i == late:
            armed.append(act)
        if t:
            s += x + i
            f = f * 0.5 + g
        g += 1.0
    return s, x, f, g, name


for fn in (release, truth):
    for k in range(N):
        fn(6, -1, None)
    print(fn.__name__, fn(6, -1, None))
    for act in (reader, writer):
        print(' ', act.__name__, fn(6, 3, act))
