"""Builtin methods bound once and called in hot loops (ins = lst.insert,
pop = lst.pop, add = s.add, get = d.get): the site meets the bound methods
of several lists, a subclass instance's, one rebound to another method of
the same list, a module function bound as a builtin, and the errors."""
import math
from support import loops


class L(list):
    pass


def churn(lst, n):
    ins = lst.insert
    pop = lst.pop
    for i in range(n):
        ins(0, i)
        if len(lst) > 5:
            pop()
    return lst


def call_all(fs, n):
    out = []
    for i in range(n):
        f = fs[i % len(fs)]
        out.append(f(i))
    return out


def run():
    a, b = [], L()
    out = [churn(a, 300), churn(b, 300), type(b).__name__]
    s, d = set(), {1: 'one'}
    fs = [s.add, d.get, math.sqrt, a.count, b.append]
    res = call_all(fs, 100)
    out.append([r if not isinstance(r, float) else round(r, 6) for r in res[:10]])
    out.append((sorted(s)[:5], len(b)))
    pop = [].pop
    try:
        pop()
    except IndexError as e:
        out.append(str(e))
    ins = a.insert
    try:
        ins('x', 1)
    except TypeError as e:
        out.append(str(e))
    return out


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
