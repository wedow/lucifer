"""Threads unpack a dict another thread keeps changing, as {**d}, f(**d),
a call with **d after other keywords, and type()'s namespace copy. The
writer adds, replaces and removes keys until the readers finish, dropping
the last reference to each value it replaces: an unpacking sees each entry
before or after a change, never a freed value or a key without its value."""
import threading

# Few reads: the oracle's lock handoffs slow it 20-50x when the host's CPUs
# are oversubscribed, and the writer races every one of them.
READS = 1000
KEYS = ['k%d' % i for i in range(12)]

d = {k: 0.0 for k in KEYS[:6]}
done = []


def writer():
    i = 0
    while not done:
        k = KEYS[i % len(KEYS)]
        d[k] = float(i)
        d.pop(KEYS[(i * 7) % len(KEYS)], None)
        i += 1


def f(**kw):
    return kw


def g(a=None, **kw):
    return kw


def good(m):
    return all(k in KEYS and type(v) is float and v >= 0.0 for k, v in m.items())


def reader(out):
    bad = 0
    for _ in range(READS):
        if not good({**d}):
            bad += 1
        if not good(f(**d)):
            bad += 1
        if not good(g(a=1, **d)):
            bad += 1
        ns = type('X', (), d).__dict__
        if not good({k: v for k, v in ns.items() if k in KEYS}):
            bad += 1
    out.append(bad)


out = []
w = threading.Thread(target=writer)
readers = [threading.Thread(target=reader, args=(out,)) for _ in range(3)]
w.start()
for t in readers:
    t.start()
for t in readers:
    t.join()
done.append(True)
w.join()
print('bad', sum(out), len(out))
