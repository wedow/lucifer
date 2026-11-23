# Compiled code that relies on what a class's instances hold under a name
# (an int, a float, objects only) while the name takes something else:
# a loop waiting on an int field ends when a callee, a finalizer or an
# __eq__ run by the loop stores a str there; a loop storing ints keeps
# storing right after another instance's name takes a str; ints and
# floats stored by compiled code into a name that holds objects read
# back as those numbers. Every value must be the interpreter's.
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


print('callee', wait_callee(N))
print('finalizer', wait_finalizer(N))
print('eq', wait_eq(N))
print('stale store', stale_store(N))
print('numbers into objects', numbers_into_objects(N))
