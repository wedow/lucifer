# Calls whose callee the code does not name: a function in a local, in an
# attribute or in a closure's cell, a bound method, a staticmethod through
# self, a closure in an attribute and closures made anew on every call, and
# a closure the caller makes and hands to a function that inlines (whose
# code is known without feedback, unless it is replaced meanwhile). The
# interpreter records the callees each call site meets, and compiled code
# inlines up to four of them behind a test of the callee. Then each site
# meets callees it was not compiled for (a fifth, a megamorphic run), and
# callees change under it: a global and an attribute rebound, a function's
# code and defaults replaced, a closure's cell rebound. Each result must be
# the interpreter's.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


def inc(x):
    return x + 1


def dec(x):
    return x - 1


def dbl(x):
    return x * 2


def neg(x):
    return -x


def sq(x):
    return x * x


def with_default(x, k=10):
    return x + k


def through_local(n, f):
    s = 0
    for i in range(n):
        s += f(i)
    return s


class Holder:
    def __init__(self, fn):
        self.fn = fn
        self.base = 3

    def run(self, n):
        s = 0
        for i in range(n):
            s += self.fn(i)
        return s

    def add(self, x):
        return x + self.base

    @staticmethod
    def static(x):
        return x - 2

    def via_static(self, n):
        s = 0
        for i in range(n):
            s += self.static(i)
        return s


def bound(n, m):
    s = 0
    for i in range(n):
        s += m(i)
    return s


def maker(k):
    def add_k(x):
        return x + k
    return add_k


def fresh(n):
    """A closure made on every iteration, called at once."""
    s = 0
    for i in range(n):
        f = maker(i % 4)
        s += f(i)
    return s


def counter():
    count = 0

    def bump(x):
        nonlocal count
        count += x
        return count
    return bump


def calls(fs, n):
    """One site, several callees."""
    s = 0
    for i in range(n):
        s += fs[i % len(fs)](i)
    return s


def midway(holder, n, other):
    """The attribute rebound, then the callee's code replaced, mid-loop."""
    s = 0
    for i in range(n):
        s += holder.fn(i)
        if i == n // 3:
            holder.fn = other
        if i == 2 * n // 3:
            other.__code__ = sq.__code__
    return s


def through_cell(f, n):
    def loop():
        s = 0
        for i in range(n):
            s += f(i)
        return s
    return loop()


h = Holder(Holder(None).add)
print('local', through_local(N, inc), through_local(N, with_default))
print('attr', Holder(dbl).run(N), h.run(N), Holder(maker(5)).run(N))
print('static', Holder(None).via_static(N))
print('bound', bound(N, Holder(None).add), bound(N, h.add))
print('fresh', fresh(N))
b = counter()
print('cell', through_local(N, b), through_local(N, b))
print('cell2', through_cell(dbl, N), through_cell(maker(7), N))
print('poly', calls([inc, dec], N), calls([inc, dec, dbl, neg], N))
print('closures', calls([maker(1), maker(2), counter()], N))
print('fifth', calls([inc, dec, dbl, neg, sq], N))
print('mega', calls([maker(i) for i in range(9)] + [inc, dbl, neg, Holder(None).add], N))
print('kinds', calls([inc, Holder(None).add, maker(3), Holder.static], N))
inc_saved = inc
inc = sq
print('rebound global', through_local(N, inc), through_local(N, inc_saved))
holder = Holder(dbl)
print('rebound attr', holder.run(N))
holder.fn = neg
print('rebound attr', holder.run(N))
print('code', through_local(N, dec))
dec.__code__ = dbl.__code__
print('code', through_local(N, dec))
print('defaults', through_local(N, with_default))
with_default.__defaults__ = (20,)
print('defaults', through_local(N, with_default))


def tri(x):
    return x * 3


def quad(x):
    return x * 4


print('midway', midway(Holder(tri), N, quad))


def for_each(items, fn):
    for x in items:
        fn(x)


def summed(items, k):
    total = [0]

    def add(x):
        total[0] += x * k
    for_each(items, add)
    for_each(items, lambda x: total.append(x) if x < 0 else None)
    return total[0]


def swapped(items):
    seen = []
    f = lambda x: seen.append(x)
    def visit(x):
        seen.append(-x)
        if x == 2:
            f.__code__ = (lambda x: seen.append(x * 100)).__code__
        return f(x)
    for_each(items, visit)
    return sum(seen)


print('made', sum(summed(range(10), k) for k in range(N // 10)))
print('swapped', sum(swapped(range(5)) for _ in range(N // 10)))
