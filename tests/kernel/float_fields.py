# Float attributes written by compiled arithmetic: the object a reader sees
# must behave exactly as CPython's. A value stored once is one object for
# every reader (a.x is a.x, id() stable, vars(), getattr, gc referents, a
# copy), a float object stored is that object (a.x is v), one result stored
# in two places is one object, a float subclass stays itself, and threads
# that race on the first read all get the same object.
import copy
import gc
import pickle
import sys
import threading

N = 3000


class Body:
    def __init__(self, x, vx):
        self.x = x
        self.vx = vx


class Pair:
    def __init__(self):
        self.a = 0.0
        self.b = 0.0


class MyFloat(float):
    pass


def advance(bodies, n, dt):
    for _ in range(n):
        for b in bodies:
            b.vx -= b.x * dt
            b.x += dt * b.vx


def store_object(b, v, n):
    # A float object, not a computed one: the field holds that object.
    for _ in range(n):
        b.x = v
    return b.x is v


def store_shared(p, n):
    # One result in two fields: one object.
    t = 0.0
    for i in range(n):
        t = i * 0.5
        p.a = t
        p.b = t
    return p.a is p.b, p.a is t


def store_shared_int(p, n):
    # The same for a wide int carried around the loop.
    t = 0
    for i in range(n):
        t = i * 1000003
        p.a = t
    return p.a is t


def store_copied(src, dst, n):
    for i in range(n):
        src.x = i * 0.25
        dst.x = src.x
    return dst.x is src.x


def store_subclass(b, n):
    for i in range(n):
        b.x = MyFloat(i)
    return type(b.x).__name__, b.x


def store_then_delete(b, n):
    for i in range(n):
        b.vx = i * 1.5
    v = b.vx
    del b.vx
    gone = not hasattr(b, 'vx')
    b.vx = v
    return gone, b.vx is v, b.vx


def fresh_fields(n):
    out = []
    for i in range(n):
        b = Body(i * 1.0, 0.5)
        b.x = b.x * 2.0
        out.append(b)
    return out


class Probe:
    def __init__(self, x, rare):
        self.x = x                  # stored raw: only exits see the parameter
        if rare:
            import gc               # compiled code leaves here, naming x and self
            self.same = self.x is x


def init_exit(n):
    seen = []
    for i in range(n):
        p = Probe(i * 0.5 + 0.25, i % 1000 == 999)
        if i % 1000 == 999:
            seen.append((p.same, p.x is p.x, p.x))
    return seen


bodies = [Body(float(i), 0.5) for i in range(5)]
advance(bodies, N, 0.01)
b = bodies[0]
# The collector's view first, before anything reads the fields.
print(sorted(type(r).__name__ for r in gc.get_referents(bodies[1])))
print([round(o.x, 6) for o in bodies])
print(b.x is b.x, id(b.x) == id(b.x), b.vx is b.vx)
x1 = b.x
print(b.x is x1, getattr(b, 'x') is x1, vars(b)['x'] is x1, b.__dict__['x'] is x1)
print(any(r is x1 for r in gc.get_referents(b)))

c = Body(1.0, 2.0)
advance([c], N, 0.001)
d = copy.copy(c)
print(d.x is c.x, d.vx == c.vx, pickle.loads(pickle.dumps(c)).x == c.x)

v = 12345.678
print(store_object(Body(0.0, 0.0), v, N))
print(store_shared(Pair(), N))
print(store_shared_int(Pair(), N))
print(store_copied(Body(0.0, 0.0), Body(0.0, 0.0), N))
print(store_subclass(Body(0.0, 0.0), N))
print(store_then_delete(Body(0.0, 0.0), N))
print(init_exit(N))
fs = fresh_fields(N)
print(fs[-1].x, fs[-1].x is fs[-1].x, sum(f.x for f in fs))

e = Body(3.0, 1.0)
advance([e], N, 0.002)
setattr(e, 'x', 7.5)
print(e.x, e.x is e.x)
advance([e], N, 0.002)
e.y = 'other'
print(sorted(vars(e)), round(e.x, 6), e.x is vars(e)['x'])

# Threads race on the first read of a field compiled code wrote.
for trial in range(20):
    f = Body(1.0, 1.0)
    advance([f], 50 + trial, 0.01)
    seen = []
    start = threading.Barrier(4)

    def read():
        start.wait()
        seen.append(id(f.x))

    ts = [threading.Thread(target=read) for _ in range(4)]
    for t in ts:
        t.start()
    for t in ts:
        t.join()
    if len(set(seen)) != 1 or seen[0] != id(f.x):
        print('race: different objects', trial)
print('race done')
