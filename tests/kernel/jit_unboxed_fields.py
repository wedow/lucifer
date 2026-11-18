# Instances compiled code made, whose fields are numbers it computed, made
# real where they escape (kept in a list, returned): an int or float field
# nothing else names is stored unboxed, and reads of it see the same
# number, type and identity the interpreter shows; one a local also names
# stays that object. tools/jitcheck runs this at two sizes and requires
# that the extra iterations box no float.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class P:
    def __init__(self, x, y):
        self.x = x
        self.y = y


def make(a, b):
    return P(a * 1.5, b + 1000)


def keep(a):
    t = a * 2.5
    p = P(t, t)
    return p, t


def run(n):
    out = []
    s = 0.0
    for i in range(n):
        p = make(i * 0.5, i)
        out.append(p)
        s += p.x + p.y
    return s, len(out)


def check():
    out = [make(i * 0.5, i) for i in range(5)]
    last = out[-1]
    q, t = keep(3.0)
    return (last.x, last.y, type(last.x).__name__, type(last.y).__name__, last.x is last.x,
            q.x is t, q.y is q.x, [p.x + p.y for p in out])


print(run(N))
print(check())
