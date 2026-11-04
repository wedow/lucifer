# Weak references under threads: readers dereference a weakref while the owner drops the
# last strong reference, with callbacks, a WeakValueDictionary and every
# dead reference accounted for. A reader that gets an object must find it whole.
import threading
import weakref


class Node:
    def __init__(self, i):
        self.i = i
        self.payload = [i, i, i, i]


N = 20000
latest = [None, None]       # the newest reference, and its key
wvd = weakref.WeakValueDictionary()
lock = threading.Lock()
fired = [0]
bad = [0]
seen = [0]
done = [False]
keep = []                   # every reference stays alive, so every callback fires


def callback(ref):
    with lock:
        fired[0] += 1


def owner():
    for i in range(N):
        node = Node(i)
        wvd[i] = node
        latest[1] = i
        latest[0] = weakref.ref(node, callback)
        keep.append(latest[0])
        del node
    done[0] = True


def reader():
    mine = 0
    wrong = 0
    while not done[0]:
        ref = latest[0]
        if ref is None:
            continue
        for found in (ref(), ref(), ref(), wvd.get(latest[1])):
            if found is not None:
                mine += 1
                if found.payload != [found.i] * 4:
                    wrong += 1
    with lock:
        seen[0] += mine
        bad[0] += wrong


threads = [threading.Thread(target=reader) for _ in range(5)]
for t in threads:
    t.start()
owner()
for t in threads:
    t.join()
print("bad", bad[0])
print("callbacks", fired[0])
print("table", len(wvd))
print("dead", sum(ref() is None for ref in keep))
