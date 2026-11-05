# Locals that are unbound when a function starts and assigned floats or
# ints inside its loop. Compiled code keeps them raw around the loop, with a
# bit for "still unbound": whatever leaves compiled code (a changed type, an
# exception, a frame that is looked at) must see each local bound to the
# value it last had, or unbound if no iteration assigned it yet.
import sys

N = 3000
R = 1200        # calls, so that each function compiles at its call's entry too


def floats(xs, show=False):
    total = 0.0
    for i in range(len(xs)):
        if i == 0 and show:
            try:
                print(d)
            except UnboundLocalError:
                print('d unbound')
        d = xs[i] * 2.0
        total += d
    return total, d


def ints(xs):
    for x in xs:
        k = x * 3
        m = k + 1
    return k, m


def changing(xs):
    out = []
    for x in xs:
        y = x * 0.5
        out.append(y)
    return out[-3:], y


def peeked(n):
    seen = []
    for i in range(n):
        if i > n - 3:
            seen.append(sorted(sys._getframe().f_locals))
        v = i * 1.5
    return seen, v


def never(xs):
    for x in xs:
        if x < 0:
            w = x * 1.0
    try:
        return w
    except UnboundLocalError:
        return 'w unbound'


def first_exit(xs):
    # The first iteration leaves compiled code before w is ever assigned.
    r = []
    for x in xs:
        r.append(type(x).__name__)
        w = x * 2
    return r[:2], w


small = [float(i) for i in range(20)]
for r in range(R):
    out = (floats(small), ints(list(range(20))), never(small), peeked(5)[1])
print(out)
for r in range(R):
    out = changing(small + ([1, 2, 3j] if r == R - 1 else []))
print(out)
for r in range(R):
    out = first_exit((['ab'] if r == R - 1 else []) + small)
print(out)
print(floats([float(i) for i in range(N)], True))
print(ints(list(range(N))))
print(changing([float(i) for i in range(N)] + [1, 2, 3j]))
print(peeked(N))
print(never([float(i) for i in range(N)]))
print(first_exit(['ab'] + [float(i) for i in range(N)]))
