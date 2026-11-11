# A WeakSet and a WeakKeyDictionary shared by threads whose objects die while
# others hash and compare references to them: every probe meets references
# whose referents are being released elsewhere, and their callbacks discard
# from the very containers being probed.
import threading
import weakref

N, T = 3000, 4
ws = weakref.WeakSet()
wk = weakref.WeakKeyDictionary()
slots = [None] * 16
errors = []


class Obj:
    pass


def worker(seed):
    try:
        for i in range(N):
            o = Obj()
            ws.add(o)
            wk[o] = i
            r = weakref.ref(o)
            if o not in ws or wk.get(o) != i or r not in {weakref.ref(o): 1}:
                errors.append('lost')
            slots[(seed * 7 + i) % len(slots)] = o      # another thread's object may die here
            del o
            for s in list(slots):
                if s is not None:
                    s in ws
    except Exception as e:
        errors.append(repr(e))


threads = [threading.Thread(target=worker, args=(n,)) for n in range(T)]
for t in threads:
    t.start()
for t in threads:
    t.join()
slots[:] = [None] * len(slots)
import gc
gc.collect()
print('errors', errors[:3], 'left', len(ws), len(wk))
