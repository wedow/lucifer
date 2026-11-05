# A list iterator one thread made and others then share: each thread's next()
# must step the shared position (every item comes out), while a list another
# thread grows is iterated by its owner without the iterator's lock.
import threading

N = 20000
items = list(range(N))
it = iter(items)
first = [next(it) for _ in range(10)]        # the owner alone, before sharing
seen = [[] for _ in range(4)]
start = threading.Barrier(4)


def drain(k):
    start.wait()
    for v in it:
        seen[k].append(v)


ts = [threading.Thread(target=drain, args=(k,)) for k in range(4)]
for t in ts:
    t.start()
for t in ts:
    t.join()
got = set(first)
for s in seen:
    got.update(s)
print(got == set(items))

grow = list(range(1000))
stop = threading.Event()


def grower():
    while not stop.is_set():
        grow.append(len(grow))
        if len(grow) > 50000:
            del grow[1000:]


g = threading.Thread(target=grower)
g.start()
ok = True
for r in range(50):
    prev = -1
    for v in grow:
        if v != prev + 1:
            ok = False
        prev = v
        if v >= 999:
            break
stop.set()
g.join()
print(ok)
