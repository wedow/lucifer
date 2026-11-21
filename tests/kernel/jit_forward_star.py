# Calls that pass a function's own *args and **kwargs on, with and without
# leading arguments: f(*args), f(*args, **kwargs), f(a, *args, **kwargs).
# Compiled code passes the tuple's items and the dict's entries on without
# making the list, tuple and dict CALL_FUNCTION_EX would: what the callee
# receives (a tuple and dict of its own, which it may change), what the
# caller's are after, the errors (keys that are not strs, a star argument
# that is not a tuple), and every kind of callee must be the interpreter's.
import functools
import sys
import traceback

N = int(sys.argv[1]) if len(sys.argv) > 1 else 400


def take(*b, **j):
    j['seen'] = len(b)
    return b, j


def plain(*a):
    return take(*a)


def both(*a, **k):
    return take(*a, **k)


def lead(x, *a, **k):
    return take(x, -1, *a, **k)


def mine():
    out = []
    for i in range(N):
        k = {'p': i} if i % 3 else {}
        a = (i, i + 1) if i % 4 else ()
        for f in (both, lead):
            b, j = f(i, *a, **k)
            out.append((len(b), sorted(j), 'seen' in k))
        b, j = plain(*a)
        out.append((b == a, b is a if not a else None, j))
    return len(out), out[:6], out[-6:]


print('mine', mine())


def keep(*a, **k):
    """A callee that keeps what it got: the caller's own may not change."""
    return a, k


def keeper(*a, **k):
    got_a, got_k = keep(*a, **k)
    got_k['extra'] = 1
    return got_a is a, got_k is k, sorted(k), sorted(got_k)


def kept():
    return [keeper(i, q=i) for i in range(N)][-1], [keeper() for i in range(N)][-1]


print('kept', kept())


class Pair:
    def __init__(self, *a, **k):
        self.a = a
        self.k = k

    def method(self, *a, **k):
        return len(a) + len(k)


def callees(*a, **k):
    p = Pair(*a, **k)
    return (max(*a[:2]), p.method(*a, **k), functools.partial(take, 0)(*a, **k)[0],
            Pair.method(p, *a, **k), str.__add__(*a[2:4]) if len(a) > 3 else None)


def many():
    out = []
    for i in range(N):
        out.append(callees(i, 2, 'x', 'y', z=i) if i % 2 else callees(i, 1))
    return out[-2:]


print('many', many())


def fwd_kw(f, *a, **k):
    return f(*a, **k)


def fwd_lead(f, *a, **k):
    return f(a[0] if a else None, *a, **k)


def errors():
    out = []
    for i in range(N):
        try:
            if i % 5 == 0:
                fwd_kw(take, 1, **{'a': 1})
                out.append(fwd_kw(take, *[1, 2])[0])
            elif i % 5 == 1:
                out.append(fwd_lead(take, *(x for x in range(3)))[0])
            elif i % 5 == 2:
                fwd_kw(take, i, **{1: 2})
            elif i % 5 == 3:
                fwd_lead(take, i, **{'seen': 1, 'x': 2})
                fwd_kw(lambda: 0, i)
            else:
                fwd_kw(take, 1, *5)
        except TypeError as e:
            out.append(str(e))
    return out[:10], len(out)


print('errors', errors())


class Tup(tuple):
    def __iter__(self):
        return iter((7, 8, 9))


class Map(dict):
    def keys(self):
        return ['k']

    def __getitem__(self, key):
        return key * 2


def subclasses():
    out = []
    for i in range(N):
        t = Tup((i,)) if i % 2 else (i,)
        m = Map(q=1) if i % 3 == 0 else {'q': i}
        out.append(fwd_kw(take, *t, **m))
        out.append(fwd_lead(take, *t, **m))
    return out[:6], out[-2:]


print('subclasses', subclasses())


def boom(*a, **k):
    if len(a) > 2:
        raise ValueError(a)
    return len(a)


def spread(*a, **k):
    return boom(
        0,
        *a,
        **k,
    )


def tracebacks():
    out = []
    for i in range(N):
        try:
            out.append(spread(*range(i % 4)))
        except ValueError as e:
            tb = e.__traceback__
            out.append([(f.name, f.lineno, f.colno, f.end_colno) for f in traceback.extract_tb(tb)])
    return out[:5], len(out)


print('tracebacks', tracebacks())


class Num:
    def __init__(self, v):
        self.v = v

    def __eq__(self, other):
        return isinstance(other, Num) and self.v == other.v

    __hash__ = None


def operators():
    out = 0
    for i in range(N):
        out += (Num(i) == Num(i)) + ('a%d' % i == 'a%d' % (i // 2)) + ((i, 'x') == (i, 'x'))
    return out


print('operators', operators())
