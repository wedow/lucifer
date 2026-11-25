# Functions made on a side thread (top-level, so deferred, and owned by that
# thread) are called hot by workers, whose compiled returns give their
# references back on the shared count; then every reference goes and a
# collection, which takes the deferred bias off, must free them and what
# their globals hold. A deferred function of the main thread called by the
# workers keeps its count: the workers make about 50,000 returns from it, and
# compiled units may hold a few references of their own, so a wrong
# subtraction would show as a drift far larger than those.
import gc
import sys
import threading
import weakref

SRC = compile('''
marker = Marker()

def leaf(x):
    return x + 1

def rec(n):
    return rec(n - 1) + 1 if n else leaf(0)
''', 'side', 'exec')


class Marker:
    pass


def main_leaf(x):
    return x + 2


def main_rec(n):
    return main_rec(n - 1) + 1 if n else main_leaf(0)


made = []


def side():
    for k in range(4):
        ns = {'Marker': Marker}
        exec(SRC, ns)
        made.append(ns)


def worker(fns, out):
    t = 0
    for i in range(5000):
        for f in fns:
            t += f(i & 3)
        t += main_rec(i & 3)
    out.append(t)


s = threading.Thread(target=side)
s.start()
s.join()
before = sys.getrefcount(main_rec)
fns = [ns['rec'] for ns in made]
refs = [weakref.ref(ns['rec']) for ns in made] + [weakref.ref(ns['marker']) for ns in made]
out = []
ws = [threading.Thread(target=worker, args=(fns, out)) for _ in range(4)]
for w in ws:
    w.start()
for w in ws:
    w.join()
after = sys.getrefcount(main_rec)
del fns, made[:]
gc.collect()
print(out, sum(r() is not None for r in refs), abs(after - before) < 1000)
