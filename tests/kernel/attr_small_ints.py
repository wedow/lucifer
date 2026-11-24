# Cached small ints stored into an int attribute by the interpreter or C
# come back as the same objects, read by the interpreter, by compiled
# code (as objects and as numbers), through vars(), getattr, copy and
# pickle; an int outside the cache comes back as the very object stored.
import copy
import pickle
import sys

try:
    from _lucifer import inline_kinds_ok
except ImportError:
    def inline_kinds_ok(o):
        return True

N = int(sys.argv[1]) if len(sys.argv) > 1 else 2000


class P:
    def __init__(self, x):
        self.x = x


def obj(p):
    return p.x


def num(p):
    return p.x + 0


def identities():
    seven = 7
    n = 256
    n += 1                              # 257, outside the cache, computed
    big = n * 1000
    neg = -5
    neg6 = neg - 1
    ps = [P(0) for _ in range(6)]
    ps[0].x = 7
    setattr(ps[1], 'x', seven)
    ps[2].x = n
    ps[3].x = big
    object.__setattr__(ps[4], 'x', neg)
    ps[5].x = neg6
    want = [7, seven, n, big, neg, neg6]
    out = [p.x is w for p, w in zip(ps, want)]
    out += [obj(p) is w for p, w in zip(ps, want)]
    out += [getattr(p, 'x') is w for p, w in zip(ps, want)]
    out += [vars(copy.copy(p))['x'] is w for p, w in zip(ps[:2] + ps[4:5], [7, seven, neg])]
    out.append([pickle.loads(pickle.dumps(p)).x for p in ps] == want)
    return all(out), all(inline_kinds_ok(p) for p in ps)


def loops(n):
    ps = [P(i) for i in range(10)]
    s = t = 0
    for i in range(n):
        p = ps[i % 10]
        p.x = i % 300                   # cached below 257, a box above
        s += num(p)
        if obj(p) is not p.x:
            t += 1
    return s, t, all(inline_kinds_ok(p) for p in ps), [p.x for p in ps]


print('identities', identities())
print('loops', loops(N))
