# Int attributes written by compiled arithmetic (tests/kernel/float_fields.py
# for ints): the object a reader sees must behave exactly as CPython's. A value stored once is one object for
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
        self.a = 0
        self.b = 0


class MyInt(int):
    pass


def advance(bodies, n, dt):
    for _ in range(n):
        for b in bodies:
            b.vx -= (b.x * dt) % 1000 - 499
            b.x += (dt * b.vx) % 100003


def store_object(b, v, n):
    # A float object, not a computed one: the field holds that object.
    for _ in range(n):
        b.x = v
    return b.x is v


def store_shared(p, n):
    # One result in two fields: one object.
    t = 0
    for i in range(n):
        t = i * 100003
        p.a = t
        p.b = t
    return p.a is p.b, p.a is t


def store_copied(src, dst, n):
    for i in range(n):
        src.x = i * 100019
        dst.x = src.x
    return dst.x is src.x


def store_subclass(b, n):
    for i in range(n):
        b.x = MyInt(i)
    return type(b.x).__name__, b.x


def store_then_delete(b, n):
    for i in range(n):
        b.vx = i * 1000003
    v = b.vx
    del b.vx
    gone = not hasattr(b, 'vx')
    b.vx = v
    return gone, b.vx is v, b.vx


def fresh_fields(n):
    out = []
    for i in range(n):
        b = Body(i * 1000, 5)
        b.x = b.x * 2000
        out.append(b)
    return out


bodies = [Body(i * 1000, 7) for i in range(5)]
advance(bodies, N, 3)
b = bodies[0]
print([o.x for o in bodies])
print(b.x is b.x, id(b.x) == id(b.x), b.vx is b.vx)
x1 = b.x
print(b.x is x1, getattr(b, 'x') is x1, vars(b)['x'] is x1, b.__dict__['x'] is x1)
print(any(r is x1 for r in gc.get_referents(b)))

c = Body(1000, 2000)
advance([c], N, 7)
d = copy.copy(c)
print(d.x is c.x, d.vx == c.vx, pickle.loads(pickle.dumps(c)).x == c.x)

v = 123456789
print(store_object(Body(0, 0), v, N))
print(store_shared(Pair(), N))
print(store_copied(Body(0, 0), Body(0, 0), N))
print(store_subclass(Body(0, 0), N))
print(store_then_delete(Body(0, 0), N))
fs = fresh_fields(N)
print(fs[-1].x, fs[-1].x is fs[-1].x, sum(f.x for f in fs))

e = Body(3000, 1000)
advance([e], N, 5)
setattr(e, 'x', 7500)
print(e.x, e.x is e.x)
advance([e], N, 5)
e.y = 'other'
print(sorted(vars(e)), e.x, e.x is vars(e)['x'])

# Threads race on the first read of a field compiled code wrote.
for trial in range(20):
    f = Body(1000, 1000)
    advance([f], 50 + trial, 3)
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
