"""Threads add, discard and remove in one set, and read it: a key added by
several threads is stored once, a key removed by several is removed once, a
resize between a probe and its commit drops no other key. Keys with a Python
__eq__ take the candidate path, which commits after __eq__ has run."""
import threading


class K:
    def __init__(self, v):
        self.v = v

    def __hash__(self):
        return self.v % 4

    def __eq__(self, other):
        return type(other) is K and self.v == other.v


THREADS = 4


def run(fn):
    bar = threading.Barrier(THREADS)
    out = [None] * THREADS

    def body(k):
        bar.wait()
        out[k] = fn(k)

    ts = [threading.Thread(target=body, args=(k,)) for k in range(THREADS)]
    for t in ts:
        t.start()
    for t in ts:
        t.join()
    return out


def ident(x):
    return ('K', x.v) if type(x) is K else x


dupes = lost = 0
for _ in range(30):
    s = set()
    run(lambda k: [s.add(i) for i in range(300)] + [s.add(K(i)) for i in range(20)])
    items = [ident(x) for x in s]
    dupes += len(items) - len(set(items))
    lost += 320 - len(set(items))
print('add', dupes, lost)

wrong = left = 0
for _ in range(30):
    s = {i for i in range(300)} | {K(i) for i in range(20)}

    def remove_all(k):
        n = 0
        for x in [i for i in range(300)] + [K(i) for i in range(20)]:
            try:
                s.remove(x)
                n += 1
            except KeyError:
                pass
        return n

    wrong += abs(sum(run(remove_all)) - 320)
    left += len(s)
print('remove', wrong, left)

stable = set(range(100))
s = set(stable)


def churn(k):
    bad = 0
    for r in range(8):
        for i in range(k * 1000 + 1000, k * 1000 + 1400):
            s.add(i)
        for i in range(k * 1000 + 1000, k * 1000 + 1400):
            if i not in s:
                bad += 1
            s.discard(i)
        for i in range(100):
            if i not in s:
                bad += 1
    return bad


print('churn', sum(run(churn)), s == stable, len(s))

src = set(range(200, 260))
off = 0
for _ in range(30):
    s = set()

    def update_or_add(k):
        if k % 2:
            s.update(src)
        else:
            for i in range(100):
                s.add(i)

    run(update_or_add)
    off += abs(len(s) - 160)
print('update', off)
