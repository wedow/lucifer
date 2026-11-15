# A flag set on some paths and tested after they join: True and False,
# comparison results, their negations, and a path where the flag is no
# bool at all; the flag read back as an object, kept past an exit, and
# compared by identity.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


def flags(n):
    hits = 0
    kept = []
    for i in range(n):
        f = False
        if i % 3 == 0:
            f = True
        g = i % 5 == 0
        if i % 7 == 0:
            g = not g
        h = f
        if i % 11 == 0:
            h = i > n // 2
        if f:
            hits += 1
        if g:
            hits += 2
        if h and not g:
            hits += 4
        if i % 501 == 0:
            kept.append((f, g, h, f is True, g is False, h is f))
    return hits, kept


def not_a_bool(n):
    out = []
    for i in range(n):
        f = False
        if i % 2:
            f = True
        if i == n - 3:
            f = 0
        if i == n - 2:
            f = 'x'
        if i == n - 1:
            f = None
        out.append(1 if f else 0)
        if i >= n - 4:
            out.append(f)
    return sum(x for x in out if isinstance(x, int)), out[-8:]


def nested(n):
    a = b = 0
    for i in range(n):
        ok = True
        if i % 2:
            if i % 3:
                ok = False
            elif i % 5:
                ok = i % 7 > 2
        else:
            ok = not (i % 4 == 0)
        if ok:
            a += 1
        else:
            b += 1
        ok = ok or i % 13 == 0
        a += ok
    return a, b


def returned(i):
    r = False
    if i % 2:
        r = True
    if i % 9 == 0:
        r = i % 4 < 2
    return r


def through_calls(n):
    t = 0
    seen = set()
    for i in range(n):
        r = returned(i)
        t += r
        seen.add(r)
        if r is True:
            t += 10
    return t, sorted(seen)


for f in (flags, not_a_bool, nested, through_calls):
    print(f.__name__, f(N))
    print(f.__name__, f(N // 7))
