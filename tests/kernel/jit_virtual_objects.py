# Objects compiled code need not make: instances whose __init__ inlined and
# tuples (a multiple return, an unpacked display, *args) that nothing
# outside compiled code sees. Each must behave as the interpreter's object
# wherever it is seen: by an exit that makes it (a guard failing while it
# lives, with the fields it has there, inside its __init__ or after), as
# one object however many locals name it (is, id, weak references, vars,
# type), and in its identity tests while compiled. tools/jitcheck also runs
# this at two sizes and requires that the objects compiled code allocates
# do not grow with the iterations.
import sys
import weakref

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class Vec:
    def __init__(self, x, y):
        self.x = x
        self.y = y

    def plus(self, other):
        return Vec(self.x + other.x, self.y + other.y)

    def dot(self, other):
        return self.x * other.x + self.y * other.y


def divmod7(a):
    return a // 7, a % 7


def total(*args):
    return args[0] + args[-1] + len(args)


def step(p, i):
    v = p.plus(Vec(i, 1))
    q, r = divmod7(i)
    a, b = b, a = (i, q)
    s = v.dot(v) % 1000 + q + r + total(i, r, 3) + a - b
    if v is p or (q, r) is None:
        s += 1
    return s


# The initializer and methods run once first, as they have by the time a
# hot loop compiles.
step(Vec(0, 0), 0)


def hot(n):
    s = 0
    p = Vec(1, 2)
    for i in range(n):
        s += step(p, i)
    return s


print('hot', hot(N))

# The rest runs a fixed number of times, whatever N is.
K = 300


class Grows:
    def __init__(self, x):
        self.x = x
        self.y = x * x


def inside():
    """An exit in __init__ makes the instance with the one field stored."""
    out = []
    x = 1
    for i in range(K):
        g = Grows(x)
        if i % 60 == 59:
            out.append((list(vars(g)), g.y % 1009, type(g).__name__))
        x = x * 5 if i % 60 == 58 else x + 1
    return out


print('inside', inside())


def after():
    """An exit after the instance is made: one object for every name."""
    out = []
    big = 1
    for i in range(K):
        v = Vec(i, -i)
        w = v
        t = (v, i)
        u = t
        big = big * 3 if i % 50 == 49 else 1
        z = v.x * big
        if i % 50 == 49:
            r = weakref.ref(v)
            out.append((v is w, id(v) == id(w), r() is w, t is u, t[0] is v, vars(w), z % 997,
                        type(u).__name__))
    return out


print('after', after())


def identity():
    n = 0
    for i in range(K):
        a = Vec(i, i)
        b = Vec(i, i)
        c = a
        t = (a, b)
        n += (a is b) + 2 * (a is c) + 4 * (a is not b) + 8 * (t is t) + 16 * (t[1] is b)
    return n


print('identity', identity())


def unpack_alias():
    out = []
    big = 1
    for i in range(K):
        t = (i, str(i))
        s = t
        x, y = t
        big = big * 7 if i % 70 == 69 else 1
        if i % 70 == 69:
            out.append((t is s, x, y, len(s), s[1] is y, i * big % 1013))
    return out


print('unpack_alias', unpack_alias())


def star(*args):
    big = args[0]
    return args[1] * big, len(args)


def stars():
    out = []
    big = 1
    for i in range(K):
        big = big * 11 if i % 90 == 89 else 1
        out.append(star(big, i, 'x'))
    return out[-3:]


print('stars', stars())


class Slotless:
    def __init__(self, a, b):
        self.b = b
        self.a = a


def order():
    out = []
    for i in range(K):
        s = Slotless(i, -i)
        if i % 100 == 0:
            out.append(list(vars(s)))
    return out


print('order', order())


class Counted:
    made = 0

    def __init__(self, v):
        self.v = v
        Counted.made += 1


def counted():
    s = 0
    for i in range(K):
        s += Counted(i).v
    return s, Counted.made


print('counted', counted())
