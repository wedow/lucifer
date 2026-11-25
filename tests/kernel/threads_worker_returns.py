# Workers return from functions the main thread defined while other threads
# rebind the global those functions read and drop and replace the
# functions themselves: every call computes with what it read, and the
# dropped functions die.
import gc
import threading
import weakref

K = 1


def one():
    return K


def make(k):
    def scaled(x):
        return x * k
    return scaled


def leaf(x):
    return x + one()


def mid(x):
    return leaf(x) + leaf(x + 1)


slot = [make(2)]
done = []


def worker(out):
    t = 0
    for i in range(20000):
        t += mid(i) - 2 * i - 3
        t += slot[0](i) - 2 * i
    out.append(t)


def rebinder():
    global K
    while not done:
        K = 1


def swapper(refs):
    while not done:
        f = make(2)
        refs.append(weakref.ref(f))
        slot[0] = f
        del f
        gc.collect()


out, refs = [], []
side = [threading.Thread(target=rebinder), threading.Thread(target=swapper, args=(refs,))]
workers = [threading.Thread(target=worker, args=(out,)) for _ in range(4)]
for t in side + workers:
    t.start()
for t in workers:
    t.join()
done.append(1)
for t in side:
    t.join()
slot[0] = None
gc.collect()
print(out, sum(r() is not None for r in refs))
