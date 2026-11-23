# Compiled code that relies on what a class's instances hold under a name
# (an int, a float, objects only) while the name takes something else:
# a loop waiting on an int field ends when a callee, a finalizer or an
# __eq__ run by the loop stores a str there; a loop storing ints keeps
# storing right after another instance's name takes a str; ints and
# floats stored by compiled code into a name that holds objects read
# back as those numbers; compiled stores of computed numbers replace
# boxes, markers and deleted words alike; values that move to a dict
# under compiled loops. Every value must be the interpreter's.
import copy
import gc
import pickle
import sys

try:
    from _lucifer import inline_kinds_ok, inline_field
except ImportError:
    def inline_kinds_ok(o):
        return True

    def inline_field(o, name):
        return None

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


class M:
    def __init__(self, i):
        self.a = i
        self.b = i * 0.5
        self.c = 'c'


class M2(M):
    pass


def moves(n):
    # Instances whose values move to a dict while compiled loops read and
    # store them: vars(), __dict__, __class__ assignment, a copy of the
    # dict, pickling; and a class whose every instance moves, every time.
    ms = [M(i) for i in range(10)]
    s = 0.0
    for i in range(n):
        m = ms[i % 10]
        s += m.a + m.b
        m.a = m.a + 1
        m.b = m.b * 0.5 + 1.0
        m.c = m.c if i % 3 else 'c'
        if i == n // 5:
            vars(ms[1])
        if i == 2 * n // 5:
            ms[2].__dict__['a'] = 1000
        if i == 3 * n // 5:
            ms[3].__class__ = M2
        if i == 4 * n // 5:
            copy.copy(ms[4].__dict__)
            ms[5] = pickle.loads(pickle.dumps(ms[5]))
    every = [M(i) for i in range(10)]
    t = 0
    for i in range(n // 10):
        for m in every:
            t += m.a + len(vars(m))
            m.a = m.a + 1
    return (all(inline_kinds_ok(m) for m in ms), round(s, 3), [m.a for m in ms], [round(m.b, 6) for m in ms],
            t, [m.a for m in every])


class Z:
    pass


def plain_after_any(n):
    # Compiled object stores into a name that took a str after ints: a
    # str goes in line leaving the raw word, an int writes it.
    zs = [Z() for _ in range(4)]
    for z in zs:
        z.x = 1
    zs[0].x = 's'
    vals = ['a', 5, 'b', 9, 300, 'c']
    last = [1] * 4
    for i in range(n):
        v = vals[i % 6]
        zs[i % 4].x = v
        if type(v) is int:
            last[i % 4] = v
    raws = [inline_field(z, 'x') for z in zs]
    return all(inline_kinds_ok(z) for z in zs), [z.x for z in zs], [r is None or r[1] == w for r, w in zip(raws, last)]


def mixed_site(n):
    # A load site whose word holds a str half the time.
    ms = [Z() for _ in range(10)]
    for k, m in enumerate(ms):
        m.x = k
    for k in range(0, 10, 2):
        ms[k].x = 's'
    s = 0
    for i in range(n):
        v = ms[i % 10].x
        if type(v) is int:
            s += v + 1
    return s


print('callee', wait_callee(N))
print('finalizer', wait_finalizer(N))
print('eq', wait_eq(N))
print('stale store', stale_store(N))
print('numbers into objects', numbers_into_objects(N))
print('raw stores', raw_stores(N))
print('many classes', many_classes(N))
print('moves', moves(N))
print('plain after any', plain_after_any(N))
print('mixed site', mixed_site(N))
