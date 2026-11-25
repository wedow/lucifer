# A namespace a hot caller on another thread learned from is freed once the
# program drops it: the main thread swaps in a new namespace's function
# while the caller runs, drops the old namespace and collects. The caller
# must compute each call with the function it read, and once it stops,
# every dropped namespace is dead. Each output line must be the
# interpreter's.
import gc
import threading
import time
import weakref

ROUNDS = 12


class Marker:
    pass


CODE = compile('''
marker = Marker()

def plain(x):
    return x * SCALE
''', 'mod', 'exec')


def load(k):
    ns = {'SCALE': k, 'Marker': Marker}
    exec(CODE, ns)
    return ns


slot = [load(1)['plain']]
progress = [0, 0]           # passes, wrong results
done = []


def run():
    while not done:
        f = slot[0]
        s = f.__globals__['SCALE']
        for i in range(200):
            progress[1] += f(i) != i * s
        progress[0] += 1


t = threading.Thread(target=run)
t.start()
refs = [weakref.ref(slot[0].__globals__['marker'])]
for k in range(2, ROUNDS + 2):
    seen = progress[0]
    while progress[0] < seen + 3:
        time.sleep(0.001)
    ns = load(k)
    refs.append(weakref.ref(ns['marker']))
    slot[0] = ns['plain']
    del ns
    gc.collect()
done.append(1)
t.join()
slot[0] = None
gc.collect()
print('wrong', progress[1], 'dead', sum(r() is None for r in refs), 'of', len(refs))
