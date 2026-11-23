# Compiled code that relies on what a class's instances hold under a name
# (an int, a float, objects only) while the name takes something else:
# a loop waiting on an int field ends when a callee, a finalizer or an
# __eq__ run by the loop stores a str there; a loop storing ints keeps
# storing right after another instance's name takes a str; ints and
# floats stored by compiled code into a name that holds objects read
# back as those numbers; compiled stores of computed numbers replace
# boxes, markers and deleted words alike. Every value must be the
# interpreter's.
import gc
import sys

try:
    from _lucifer import inline_kinds_ok
except ImportError:
    def inline_kinds_ok(o):
        return True

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class Holder:
    def __init__(self):
        self.state = 0
        self.ticks = 0


def wait_callee(n):
    h = Holder()

    def tick():
        h.ticks += 1
        if h.ticks == n:
            h.state = 'done'

    while h.state == 0:
        tick()
    return h.state, h.ticks


class Dropped:
    def __init__(self, h, last):
        self.h = h
        self.last = last

    def __del__(self):
        if self.last:
            self.h.state = 'done'


def wait_finalizer(n):
    h = Holder()
    while h.state == 0:
        h.ticks += 1
        Dropped(h, h.ticks == n)
    return h.state, h.ticks


class Sets:
    def __init__(self, h, n):
        self.h = h
        self.n = n

    def __eq__(self, other):
        self.h.ticks += 1
        if self.h.ticks == self.n:
            self.h.state = 'done'
        return False


def wait_eq(n):
    h = Holder()
    s = Sets(h, n)
    while h.state == 0:
        if s == 1:
            break
    return h.state, h.ticks


class P:
    def __init__(self, x):
        self.x = x


def stale_store(n):
    ps = [P(i) for i in range(10)]
    for i in range(n):
        p = ps[i % 10]
        p.x = i + 1
        if i == n // 2:
            ps[(i + 1) % 10].x = 'str'
        if i == n // 2 + 3:
            ps[(i + 1) % 10].x = 2.5
        if i > n // 2:
            q = P(0)                    # a fresh instance stored to after the name took anything
            q.x = i + 2
            ps[i % 10 - 1].x = q.x
    return all(inline_kinds_ok(p) for p in ps), [p.x for p in ps]


class O:
    def __init__(self, x):
        self.x = x


def numbers_into_objects(n):
    os_ = [O('s') for i in range(10)]
    for i in range(n):
        o = os_[i % 10]
        o.x = i * 3 + 1
        if i % 7 == 0:
            o.x = i * 0.5
    return all(inline_kinds_ok(o) for o in os_), [o.x for o in os_], sum(o.x for o in os_)


class F:
    def __init__(self, x, n):
        self.x = x
        self.n = n


def raw_stores(n):
    # Compiled stores of computed numbers into words that hold the
    # interpreter's boxes, their own markers, or nothing after a deletion.
    fs = [F(i * 0.5, i) for i in range(10)]
    for i in range(n):
        f = fs[i % 10]
        f.x = f.x * 0.5 + i
        f.n = f.n + i
        if i == n // 3:
            del fs[3].x
            del fs[4].n
            fs[3].x = 1.5
            fs[4].n = 4
    return all(inline_kinds_ok(f) for f in fs), [f.x for f in fs], [f.n for f in fs]


def make_class(i):
    class K:
        def __init__(self):
            self.x = i

        def bump(self, d):
            self.x = self.x + d
    return K


BUMP = '''
def bump_all(objs, n):
    for r in range(n):
        for o in objs:
            o.bump(1)
'''


def many_classes(n):
    # More classes than the version map's first size, half of them freed
    # (their entries leave, later ones move back), then code compiled
    # after the frees for the rest and for new ones, and a site whose
    # cached class died before it was compiled.
    objs = [make_class(i)() for i in range(300)]
    ns = {}
    exec(BUMP, ns)
    ns['bump_all'](objs, n // 100)
    objs = objs[1::2]
    gc.collect()
    objs += [make_class(i)() for i in range(40)]
    ns2 = {}
    exec(BUMP, ns2)
    ns2['bump_all'](objs, n // 100)
    dead = make_class(-1)()
    ns3 = {}
    exec(BUMP, ns3)
    ns3['bump_all']([dead], 3)          # the site caches dead's class
    del dead
    gc.collect()
    ns3['bump_all'](objs, n // 100)
    return all(inline_kinds_ok(o) for o in objs), sum(o.x for o in objs)


print('callee', wait_callee(N))
print('finalizer', wait_finalizer(N))
print('eq', wait_eq(N))
print('stale store', stale_store(N))
print('numbers into objects', numbers_into_objects(N))
print('raw stores', raw_stores(N))
print('many classes', many_classes(N))
