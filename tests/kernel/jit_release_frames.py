# The frame compiled code runs in, observed from Python that a release or a
# truth test runs: a finalizer run when compiled code drops the last
# reference (a local stored over or deleted, an attribute's or a list item's
# old value, a call's discarded result), a weakref callback, and __bool__ in
# an `if`. Each hot function runs a probe on one late iteration: it reads the
# caller's locals and line, takes its stack, or writes one of its locals
# through f_locals (PEP 667), which the function must see from then on.
# Each release shares its line with a call (abs), after which the
# interpreter's frame shows that line too: it saves its position at calls,
# not at releases. Each output line must be the interpreter's.
import sys
import traceback
import weakref

N = int(sys.argv[1]) if len(sys.argv) > 1 else 300

armed = []


def probe(frame):
    if armed:
        armed.pop()(frame)


def reader(frame):
    seen = sorted((k, v) for k, v in frame.f_locals.items() if isinstance(v, (int, float, str)))
    print('  read', frame.f_code.co_name, frame.f_lineno - frame.f_code.co_firstlineno, seen)


def writer(frame):
    frame.f_locals['x'] = 100


def stacker(frame):
    names = [(s.name, s.lineno - frame.f_code.co_firstlineno) for s in traceback.extract_stack(frame)[-2:]]
    print('  stack', names[-1], names[0][0])


class Probe:
    def __del__(self):
        probe(sys._getframe(1))


class Holder:
    def __init__(self):
        self.item = None


def make():
    return Probe()


def store_over(n, late, act):
    x = 1
    s = 0
    for i in range(n):
        p = Probe()
        if i == late:
            armed.append(act)
        k = abs(i); p = None
        s += x
    return s, x


def delete(n, late, act):
    x = 1
    s = 0
    for i in range(n):
        p = Probe()
        if i == late:
            armed.append(act)
        k = abs(i); del p
        s += x
    return s, x


def attr_store(n, late, act):
    x = 1
    s = 0
    h = Holder()
    for i in range(n):
        h.item = Probe()
        if i == late:
            armed.append(act)
        k = abs(i); h.item = None
        s += x
    return s, x


def item_store(n, late, act):
    x = 1
    s = 0
    items = [None]
    for i in range(n):
        items[0] = Probe()
        if i == late:
            armed.append(act)
        k = abs(i); items[0] = None
        s += x
    return s, x


def discarded(n, late, act):
    x = 1
    s = 0
    for i in range(n):
        if i == late:
            armed.append(act)
        make()
        s += x
    return s, x


class Target:
    pass


def callback(ref):
    probe(sys._getframe(1))


def weak(n, late, act):
    x = 1
    s = 0
    for i in range(n):
        t = Target()
        r = weakref.ref(t, callback)
        if i == late:
            armed.append(act)
        k = abs(i); t = None
        s += x
    return s, x


class Truthy:
    def __bool__(self):
        probe(sys._getframe(1))
        return True


def branch(n, late, act):
    x = 1
    s = 0
    t = Truthy()
    for i in range(n):
        if i == late:
            armed.append(act)
        if t:
            s += x
    return s, x


for fn in (store_over, delete, attr_store, item_store, discarded, weak, branch):
    for k in range(N):
        fn(6, -1, None)
    print(fn.__name__)
    for act in (reader, stacker, writer):
        print(' ', act.__name__, fn(6, 3, act))
