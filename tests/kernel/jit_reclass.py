# An object's class reassigned behind compiled code's back: by a call that
# is not inlined, by a finalizer, by a callee several frames down. What a
# unit knew of the object's type (from an operator, a method call, type(),
# isinstance, iteration) is the new class's answer afterwards.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class A:
    def __init__(self, v):
        self.v = v

    def name(self):
        return 'A'

    def __add__(self, other):
        return ('A+', self.v)

    def __len__(self):
        return 1

    def __iter__(self):
        return iter('a')

    def __bool__(self):
        return True


class B:
    def __init__(self, v):
        self.v = v

    def name(self):
        return 'B'

    def __add__(self, other):
        return ('B+', self.v)

    def __len__(self):
        return 2

    def __iter__(self):
        return iter('bb')

    def __bool__(self):
        return False


def swap(o, cls, depth):
    # Recursive, so that it stays a call.
    if depth:
        return swap(o, cls, depth - 1)
    o.__class__ = cls


def by_call(n):
    o = A(1)
    out = []
    for i in range(n):
        before = (type(o).__name__, o.name(), o + 1, len(o), bool(o), isinstance(o, A), list(o))
        if i >= n - 6:
            swap(o, B if i % 2 else A, 2)
        after = (type(o).__name__, o.name(), o + 1, len(o), bool(o), isinstance(o, A), list(o))
        if i >= n - 8:
            out.append((before, after))
    return out


class Trigger:
    def __init__(self, target, cls):
        self.target = target
        self.cls = cls

    def __del__(self):
        self.target.__class__ = self.cls


def by_finalizer(n):
    o = A(1)
    out = []
    for i in range(n):
        t = Trigger(o, B if i % 2 else A) if i >= n - 6 else None
        x = o + 1
        t = None            # the finalizer reclasses o here
        y = o + 1
        z = (type(o).__name__, o.name(), len(o), bool(o))
        if i >= n - 8:
            out.append((x, y, z))
    return out


def by_setattr(n):
    o = A(1)
    out = []
    for i in range(n):
        a = o.name()
        if i >= n - 5:
            setattr(o, '__class__', B if i % 2 else A)
        out.append((a, o.name(), type(o) is A, o + 0))
    return out[-7:]


def flip(o, i, n, depth):
    # Called on every turn, so the call is compiled; it stays a call.
    if depth:
        return flip(o, i, n, depth - 1)
    if i >= n - 6:
        o.__class__ = B if i % 2 else A


def type_only(n):
    # Nothing here reads the class: only the object's type is known.
    o = A(1)
    out = []
    for i in range(n):
        a = type(o)
        flip(o, i, n, 2)
        b = type(o)
        out.append((a is A, b is A, a is b))
    return out[-8:], len(out)


def kind_only(n):
    o = A(1)
    hits = 0
    for i in range(n):
        hits += isinstance(o, A) + 2 * isinstance(o, B)
        flip(o, i, n, 2)
        hits += 4 * isinstance(o, A) + 8 * isinstance(o, B)
    return hits


for f in (by_call, by_finalizer, by_setattr, type_only, kind_only):
    print(f.__name__, f(N))
    print(f.__name__, f(N // 5))
    print(f.__name__, f(20 * N))
