# One object named by several slots of the frames while compiled code
# calls C that may count its references: the frames' slots hold a
# reference each (published calls take them as holds and drop them after),
# so every count C reads is the interpreter's: each loop reads one count
# however its turns run (interpreted, compiled, leaving at a guard). The objects: this thread's,
# another thread's (its counts are shared), an immortal, and one named by
# forty slots; the counts are read by C called straight from the loop and
# from inside a Python callee, and the callees grow a list several names
# hold and append to a str two names hold. tools/jitcheck runs the first
# loop alone (argument 'named') at two sizes: one hold step per object a
# call is handed, however many slots name it.
import sys
import threading

grc = sys.getrefcount
N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class C:
    pass


def named(o, n):
    a = o
    b = a
    c = b
    first = grc(c)
    t = 0
    for i in range(n):
        a = o
        b = a
        c = b
        t += grc(c)
    return t == n * first


def inner(x):
    return grc(x)


def called(o, n):
    seen = set()
    for i in range(n):
        a = o
        b = o
        seen.add(inner(a) - grc(b))
    return len(seen)


def forty(o, n):
    seen = set()
    for i in range(n):
        a0 = a1 = a2 = a3 = a4 = a5 = a6 = a7 = a8 = a9 = o
        b0 = b1 = b2 = b3 = b4 = b5 = b6 = b7 = b8 = b9 = o
        c0 = c1 = c2 = c3 = c4 = c5 = c6 = c7 = c8 = c9 = o
        d0 = d1 = d2 = d3 = d4 = d5 = d6 = d7 = d8 = d9 = o
        seen.add((grc(o), inner(d9)))
    return len(seen)


def grown(xs, k):
    for j in range(k):
        xs.append(j)
    return len(xs)


def lists(n):
    seen = set()
    for i in range(n):
        xs = []
        ys = xs
        zs = ys
        seen.add((grown(xs, 9), grc(zs), len(ys)))
    return len(seen), sorted({(g, k) for g, c, k in seen})


def suffixed(s):
    s += 'x'
    return s


def strs(n):
    seen = set()
    for i in range(n):
        s = str(i % 7)
        t = s
        u = suffixed(s)
        seen.add((s == t, u == t + 'x', grc(t) - grc(s)))
    return sorted(seen)


def made_elsewhere():
    box = []
    th = threading.Thread(target=lambda: box.append(C()))
    th.start()
    th.join()
    return box.pop()


mine = C()
if sys.argv[2:] == ['named']:
    print('named', named(mine, N))
    sys.exit()
theirs = made_elsewhere()
base = {'mine': grc(mine), 'theirs': grc(theirs), 'none': grc(None)}
for label, o in (('mine', mine), ('theirs', theirs), ('none', None)):
    print(label, named(o, N), called(o, N // 10), forty(o, N // 10))
print('counts back', {'mine': grc(mine), 'theirs': grc(theirs), 'none': grc(None)} == base)
print('lists', lists(N // 10))
print('strs', strs(N // 10))
