# Numbers in instance attributes that compiled code reads and writes as
# numbers: what the program sees is what the interpreter shows. A value
# stored from arithmetic and read back; ints past one digit and past 64
# bits; an attribute that changes kind under a loop that expected a
# number; identity of a value loaded once and looked at twice; an
# attribute deleted, or the instance's class changed, between two reads;
# a finalizer that runs when a store drops the old value.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class P:
    def __init__(self, x, v):
        self.x = x
        self.v = v


class Q:
    def __init__(self, x, v):
        self.pad = None
        self.v = v
        self.x = x


def step(p):
    p.x += p.v
    if p.x > 500:
        p.x = 500
        p.v = -abs(p.v)
    if p.x < 0:
        p.x = 0
        p.v = abs(p.v)
    return p.x


def ints(n):
    p = P(3, 7)
    total = 0
    for i in range(n):
        total += step(p)
    return total, p.x, p.v, type(p.x), type(p.v)


def floats(n):
    p = P(0.5, 0.25)
    total = 0.0
    for i in range(n):
        p.x = p.x * 1.0001 + p.v
        total += p.x - p.v
    return total, p.x, type(p.x)


def wide(n):
    p = P(1, 3)
    out = []
    for i in range(n):
        p.x = p.x * 3 + p.v
        if p.x > 10 ** 30:
            out.append(p.x)
            p.x = 2 ** 40 + i
    return len(out), out[:2], p.x


def boxed_wide(n):
    p = P(2 ** 45, 2 ** 33)
    acc = 0
    for i in range(n):
        acc += p.x - p.v
    q = P(-(2 ** 62), 1)
    for i in range(n):
        acc += q.x + q.v
    return acc


def changes_kind(n):
    p = P(1, 1)
    out = []
    for i in range(n):
        if i == n - 40:
            p.x = 1.5
        if i == n - 20:
            p.x = "s"
        if i == n - 10:
            p.x = True
        try:
            out.append(p.x + p.v)
        except TypeError as e:
            out.append(type(e).__name__)
            p.x = 2 ** 70
        p.v = p.v + 1
    return out[-45:], p.x, p.v


def identity(n):
    p = P(10 ** 6, 1)
    out = []
    for i in range(n):
        a = p.x
        b = p.x
        out.append(a is b)
        out.append(a is p.x)
        p.v = p.v + i
        c = p.v
        out.append(c is p.v)
        d = p.x + 0
        p.x = d
        out.append(d is p.x)
    return all(out), len(out)


def deleted(n):
    p = P(1, 2)
    out = []
    for i in range(n):
        if i == n - 5:
            del p.x
        try:
            out.append(p.x + p.v)
        except AttributeError as e:
            out.append(str(e))
            p.x = i
    return out[-7:]


def reclassed(n):
    p = P(1, 2)
    acc = 0
    for i in range(n):
        if i == n - 5:
            p.__class__ = Q
        if i == n - 3:
            p = Q(5, 6)
        acc += p.x * 2 + p.v
        p.x = p.x + 1
    return acc, type(p).__name__, sorted(p.__dict__.items())


class Loud:
    count = 0

    def __init__(self, p):
        self.p = p

    def __del__(self):
        Loud.count += 1
        self.p.v = self.p.v + 100


def finalized(n):
    p = P(0, 1)
    acc = 0
    for i in range(n):
        if i % 500 == 250:
            p.x = Loud(p)
        if i % 500 == 251:
            p.x = i + p.v       # drops the Loud: its finalizer writes p.v
        acc += p.v
        if type(p.x) is int:
            acc += p.x
            p.x = p.x + 1
    return acc, p.v, Loud.count


def shared_dict(n):
    p = P(1, 2)
    acc = 0
    for i in range(n):
        p.x = p.x + p.v
        if i == n - 4:
            d = p.__dict__
            d["x"] = d["x"] * 2
            d["w"] = 9
        acc += p.x
    return acc, sorted(p.__dict__.items())


def held(x):
    return x


def locals_identity(n):
    # A number read into a local, or passed to a call, is the attribute's
    # own object for as long as the attribute keeps it.
    p = P(10 ** 9 + 7, 2.5)
    keep = p.x
    out = []
    for i in range(n):
        a = p.x
        b = p.v
        c = held(p.x)
        t = a + 1 + c
        u = b * 2.0
        if i % 997 == 5:
            out.append((a is p.x, a is keep, b is p.v, c is a, t, u))
        if i == n // 2:
            p.v = p.v + 1.0     # stored as a number: a new object for every reader after
            w = p.v
            out.append((w is p.v, w is b, w))
    return out[:4], out[-2:], len(out)


def locals_exit(n):
    # The loop leaves compiled code with the numbers in locals.
    p = P(2 ** 40, 0.5)
    out = []
    for i in range(n):
        a = p.x
        b = p.v
        s = a + i
        if i == n - 2:
            a = [a, b, s]       # an unexpected kind
            out.append((a[0] is p.x, a[1] is p.v, a[0], a[1], a[2]))
        p.v = b + 0.5
        d = p.v
        if i == n - 1:
            out.append((d is p.v, d))
    return out


def locals_changed(n):
    # The attribute moves on while a local still holds its old number.
    p = P(1, 1.5)
    seen = []
    for i in range(n):
        a = p.x
        b = p.v
        p.x = a + 1
        p.v = b + 1.0
        if i >= n - 3:
            seen.append((a, b, p.x, p.v, a is p.x, b is p.v))
    return seen


def abs_and_calls(n):
    p = P(-7, -2.5)
    acc = 0
    for i in range(n):
        acc += abs(p.x) + int(abs(p.v)) + max(p.x, 3) + held(p.x)
        p.x = -abs(p.x) - (i % 3)
    return acc, p.x


for f in (ints, floats, wide, boxed_wide, changes_kind, identity, deleted, reclassed, finalized, shared_dict,
          locals_identity, locals_exit, locals_changed, abs_and_calls):
    print(f.__name__, f(N))
    print(f.__name__, f(N // 3))
