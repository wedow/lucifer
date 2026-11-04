"""Readers that hold no lock against writers that replace a container's
storage and free its items: the item a reader takes must be alive (try-incref),
a dict's delete and store must act on the key they found even when another
thread resized the table in between, and a set must keep every stable key
findable while it resizes. Values are fresh tuples (i, i), so a freed or
swapped one shows as a torn pair or a crash."""
import sys
import threading
import types


def race(writer, readers):
    stop = []

    def run():
        while not stop:
            writer()

    t = threading.Thread(target=run)
    t.start()
    results = []
    ts = [threading.Thread(target=lambda r=r: results.append(r())) for r in readers]
    for x in ts:
        x.start()
    for x in ts:
        x.join()
    stop.append(True)
    t.join()
    return results


def torn(v):
    return not (type(v) is tuple and len(v) == 2 and v[0] == v[1])


# dict: values replaced, keys deleted and added, tables grown and cleared.
d = {}
for k in range(40):
    d['s%d' % k] = (k, k)
    d[k] = (k, k)
counter = [0]


def churn_dict():
    n = counter[0] = counter[0] + 1
    for k in range(0, 40, 3):
        d['s%d' % k] = (n, n)
        d[k] = (n, n)
    for k in range(1, 40, 3):
        d.pop('s%d' % k, None)
        d.pop(k, None)
    for k in range(1000, 1000 + n % 300):
        d['x%d' % k] = (k, k)
    for k in range(1000, 1000 + n % 300):
        del d['x%d' % k]
    for k in range(1, 40, 3):
        d['s%d' % k] = (k, k)
        d[k] = (k, k)
    if n % 50 == 0:
        d.clear()
        for k in range(40):
            d['s%d' % k] = (k, k)
            d[k] = (k, k)


def read_dict():
    bad = 0
    for _ in range(15000):
        for k in (3, 's5', 12, 's30'):
            v = d.get(k)
            if v is not None and torn(v):
                bad += 1
            try:
                if torn(d[k]):
                    bad += 1
            except KeyError:
                pass
    return bad


print('dict values', race(churn_dict, [read_dict] * 3))

# Module globals and attributes, which the interpreter reads without a call.
m = types.ModuleType('m')
m.a = (0, 0)
G = [0]


def churn_module():
    n = G[0] = G[0] + 1
    m.a = (n, n)
    m.b = (n, n)
    del m.b
    for k in range(n % 40):
        setattr(m, 'f%d' % k, (k, k))
    for k in range(n % 40):
        delattr(m, 'f%d' % k)


def read_module():
    bad = 0
    for _ in range(15000):
        if torn(m.a):
            bad += 1
        try:
            if torn(m.b):
                bad += 1
        except AttributeError:
            pass
    return bad


print('module attrs', race(churn_module, [read_module] * 3))

glob = sys.modules[__name__].__dict__
glob['gv'] = (0, 0)


def churn_globals():
    n = G[0] = G[0] + 1
    glob['gv'] = (n, n)
    glob['gw'] = (n, n)
    del glob['gw']
    for k in range(n % 40):
        glob['gx%d' % k] = (k, k)
    for k in range(n % 40):
        del glob['gx%d' % k]


def read_globals():
    bad = 0
    for _ in range(15000):
        if torn(gv):
            bad += 1
        try:
            if torn(gw):
                bad += 1
        except NameError:
            pass
    return bad


print('globals', race(churn_globals, [read_globals] * 3))


class Plain:
    pass


o = Plain()
for k in range(10):
    setattr(o, 'p%d' % k, (k, k))


def churn_attrs():
    n = G[0] = G[0] + 1
    for k in range(10):
        setattr(o, 'p%d' % k, (n, n))
    for k in range(n % 30):
        setattr(o, 'q%d' % k, (k, k))
    for k in range(n % 30):
        delattr(o, 'q%d' % k)


def read_attrs():
    bad = 0
    for _ in range(15000):
        if torn(o.p3) or torn(o.p7):
            bad += 1
    return bad


print('instance attrs', race(churn_attrs, [read_attrs] * 3))

# A set that resizes while its stable keys are looked up: none may go missing.
S = set(range(100, 140)) | {'k%d' % i for i in range(40)}
SG = [0]


def churn_set():
    n = SG[0] = SG[0] + 1
    size = 50 + n % 400
    for k in range(1000, 1000 + size):
        S.add(k)
    for k in range(1000, 1000 + size):
        S.discard(k)


def read_set():
    missing = 0
    for _ in range(5000):
        for k in (100, 117, 139, 'k0', 'k33'):
            if k not in S:
                missing += 1
    return missing


print('set stable keys', race(churn_set, [read_set] * 3))

# A list read while it is replaced, shrunk and grown.
L = [(i, i) for i in range(30)]
LG = [0]


def churn_list():
    n = LG[0] = LG[0] + 1
    L.extend((i, i) for i in range(n % 200))
    del L[30:]
    for i in range(30):
        L[i] = (n, n)


def read_list():
    bad = 0
    for _ in range(15000):
        a, b = L[0], L[29]
        if torn(a) or torn(b):
            bad += 1
        for v in L:
            if torn(v):
                bad += 1
                break
    return bad


print('list items', race(churn_list, [read_list] * 3))

# Four threads pop their own attributes of one object while a fifth keeps
# adding names, which resizes and compacts the table under their lookups.
ROUNDS = 12
lost = 0
wrong = 0
for _ in range(ROUNDS):
    p = Plain()
    for k in range(240):
        setattr(p, 'v%d' % k, k)
    got = []
    adding = []

    def popper(t):
        for k in range(t, 240, 4):
            got.append((k, p.__dict__.pop('v%d' % k)))

    def adder():
        i = 0
        while not adding:
            setattr(p, 'z%d' % (i % 64), i)
            i += 1

    a = threading.Thread(target=adder)
    a.start()
    ts = [threading.Thread(target=popper, args=(t,)) for t in range(4)]
    for t in ts:
        t.start()
    for t in ts:
        t.join()
    adding.append(True)
    a.join()
    left = [name for name in p.__dict__ if name.startswith('v')]
    lost += len(left)
    wrong += sum(1 for k, v in got if k != v)
    wrong += abs(len(got) - 240)
print('pop', lost, wrong)

# The same with plain dict.pop and setdefault.
lost = 0
dupes = 0
for _ in range(ROUNDS):
    q = {('v', k): k for k in range(240)}
    taken = []

    def pop_items(t):
        for k in range(t, 240, 4):
            taken.append(q.pop(('v', k)))

    def grow():
        for i in range(2000):
            q.setdefault(('z', i % 100), i)

    ts = [threading.Thread(target=pop_items, args=(t,)) for t in range(4)]
    ts.append(threading.Thread(target=grow))
    for t in ts:
        t.start()
    for t in ts:
        t.join()
    lost += sum(1 for key in q if key[0] == 'v')
    dupes += len(taken) - len(set(taken))
    dupes += abs(len(taken) - 240)
print('dict pop', lost, dupes)
