# deque as a queue and a ring: long runs of operations at both ends checked
# against a list model, index errors past a C ssize_t, mutation during
# iteration, cycles through the collector, and threads sharing one deque
# (each method is atomic in 3.14t).
import gc
import threading
import weakref
from collections import deque

seed = 4242


def rnd(n):
    global seed
    seed = (seed * 1103515245 + 12345) % 2147483648
    return (seed >> 8) % n


for maxlen in (None, 0, 1, 7, 100):
    d, model = deque(maxlen=maxlen), []
    trace = []
    for step in range(6000):
        op = rnd(14)
        if op < 4:
            d.append(step)
            model.append(step)
            if maxlen is not None and len(model) > maxlen:
                del model[0]
        elif op < 6:
            d.appendleft(step)
            model.insert(0, step)
            if maxlen is not None and len(model) > maxlen:
                del model[-1]
        elif op < 8:
            got = d.popleft() if d else None
            want = model.pop(0) if model else None
            trace.append(got == want)
        elif op == 8:
            got = d.pop() if d else None
            want = model.pop() if model else None
            trace.append(got == want)
        elif op == 9 and model:
            k = rnd(2 * len(model) + 3) - len(model) - 1
            d.rotate(k)
            if model:
                k %= len(model)
                model[:] = model[-k:] + model[:-k] if k else model
        elif op == 10 and model:
            i = rnd(len(model))
            del d[i]
            del model[i]
        elif op == 11 and (maxlen is None or len(model) < maxlen):
            i = rnd(len(model) + 5) - 2
            d.insert(i, -step)
            model.insert(i, -step)
        elif op == 12 and model:
            i = rnd(len(model))
            d[i] = d[-1 - i]
            model[i] = model[-1 - i]
        elif op == 13 and rnd(50) == 0:
            a, b = rnd(300), rnd(40)
            d.extend(range(a))
            d.extendleft(list(range(b)))
            d.reverse()
            for x in range(a):
                model.append(x)
                if maxlen is not None and len(model) > maxlen:
                    del model[0]
            for x in range(b):
                model.insert(0, x)
                if maxlen is not None and len(model) > maxlen:
                    del model[-1]
            model.reverse()
        if list(d) != model or len(d) != len(model):
            print('diverged', maxlen, step, op)
            break
    print(maxlen, len(d), all(trace), len(trace), list(d)[:5], list(d)[-5:], d.count(1), sum(d) % 1000)

q = deque()
for i in range(100000):
    q.append(i)
total = 0
while q:
    total += q.popleft()
    if total % 7 == 0:
        q.append(total % 1000)
print(total, len(q), q)

d = deque(range(5))
for f in (lambda: d.rotate(2 ** 100), lambda: d.rotate(-2 ** 63), lambda: d.rotate(2 ** 63 - 1), lambda: d.insert(2 ** 100, 9),
          lambda: d[2 ** 100], lambda: d[-2 ** 100], lambda: d.__setitem__(2 ** 100, 1), lambda: d.__delitem__(-2 ** 100),
          lambda: deque([1]).rotate(2 ** 100), lambda: deque().rotate(2 ** 100), lambda: d[5], lambda: d[-6],
          lambda: d.__setitem__(-6, 0), lambda: d.__delitem__(5)):
    try:
        print(f(), d)
    except (IndexError, OverflowError) as e:
        print(type(e).__name__, e)

for mutate in (lambda d: d.append(0), lambda d: d.pop(), lambda d: d.rotate(1), lambda d: d.clear(),
               lambda d: d.__setitem__(0, 9), lambda d: d.reverse(), lambda d: d.__delitem__(0), lambda d: d.insert(0, 1)):
    for make in (iter, reversed):
        d = deque(range(4))
        it = make(d)
        first = next(it)
        mutate(d)
        try:
            print(first, list(it))
        except RuntimeError as e:
            try:
                rest = list(it)
            except RuntimeError:
                rest = 'again'
            print(first, 'RuntimeError', e, rest)
it = iter(deque())
print(list(it), next(it, 'end'))


class Node:
    pass


d = deque()
n = Node()
d.append(n)
n.d = d
d.append(d)
w = weakref.ref(n)
del d, n
gc.collect()
print('collected', w() is None)

shared = deque()
received = []
lock = threading.Lock()
N, M = 4, 5000


def produce(base):
    for i in range(M):
        shared.append(base + i)
        if i % 3 == 0:
            shared.appendleft(-(base + i) - 1)


def consume():
    got = []
    misses = 0
    while misses < 2000:
        try:
            got.append(shared.popleft())
            misses = 0
        except IndexError:
            misses += 1
    with lock:
        received.extend(got)


threads = [threading.Thread(target=produce, args=(k * M,)) for k in range(N)]
threads += [threading.Thread(target=consume) for _ in range(2)]
for t in threads:
    t.start()
for t in threads:
    t.join()
received.extend(shared)
expected = sorted(list(range(N * M)) + [-(k * M + i) - 1 for k in range(N) for i in range(0, M, 3)])
print('threads', len(received), sorted(received) == expected)

bounded = deque(maxlen=50)


def fill(base):
    for i in range(3000):
        bounded.append(base + i)
        bounded.appendleft(base - i)


threads = [threading.Thread(target=fill, args=(k * 10000,)) for k in range(4)]
for t in threads:
    t.start()
for t in threads:
    t.join()
print('bounded', len(bounded), bounded.maxlen)
