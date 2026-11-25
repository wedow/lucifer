# Units die and are freed while other threads stand inside them: workers
# run chains of calls several functions deep (each level its own function,
# recursing through the others, so that compiled callers enter compiled
# callees directly) and resume generators, from compiled code and from C;
# a writer keeps rebinding a global every level reads (each rebinding kills
# the units that folded it); a loader keeps making namespaces whose
# function the chains call, then drops the old one and collects, which
# kills and frees the units that learned it. Every worker's sum is the same
# whichever function the global or the slot names, and once the threads
# stop every dropped namespace is dead.
import gc
import threading
import weakref


def one():
    return 1


def uno():
    return 1


F = one
ROUNDS = 400


class Marker:
    pass


CODE = compile('''
marker = Marker()

def leaf(x):
    return x + F()
''', 'mod', 'exec')


def load():
    ns = {'Marker': Marker, 'F': one}
    exec(CODE, ns)
    return ns


slot = [load()['leaf']]


def a(k, x):
    return slot[0](x) if k == 0 else b(k - 1, x) + F()


def b(k, x):
    return a(k, x) if k & 1 else c(k, x + F())


def c(k, x):
    return a(k - 1, x) + F() if k > 0 else slot[0](x)


def steps(n):
    for i in range(n):
        yield a(i & 3, i) + F()


def chain(i):
    return a(i % 9, i)


def worker(out, k):
    total = 0
    for i in range(ROUNDS):
        total += chain(i) + sum(map(chain, range(i & 7)))
        g = steps(5)
        total += next(g)
        for v in g:
            total += v
        total += sum(steps(i & 3))
    out[k] = total


def writer(stop):
    global F
    while not stop.is_set():
        F = uno if F is one else one


def loader(stop, refs):
    while not stop.is_set():
        ns = load()
        refs.append(weakref.ref(ns['marker']))
        slot[0] = ns['leaf']
        del ns
        gc.collect()


def main():
    out = [None] * 6
    refs = []
    stop = threading.Event()
    side = [threading.Thread(target=writer, args=(stop,)), threading.Thread(target=loader, args=(stop, refs))]
    workers = [threading.Thread(target=worker, args=(out, k)) for k in range(6)]
    for t in side + workers:
        t.start()
    for t in workers:
        t.join()
    stop.set()
    for t in side:
        t.join()
    print(out)
    slot[0] = None
    gc.collect()
    print('namespaces left', sum(r() is not None for r in refs))


main()
