"""Lock-free reads of shared containers while another thread mutates them:
dict lookups whose entry a deletion clears or a resize moves, and `in` on a
list whose array a resize replaces; attribute stores into a shared instance
dict, and the first ones, which make it."""
import threading


def race(writer, reader):
    stop = []

    def run():
        while not stop:
            writer()

    t = threading.Thread(target=run)
    t.start()
    try:
        return reader()
    finally:
        stop.append(True)
        t.join()


# Deleting and re-adding 'k' and 'z' clears entries and, at each resize,
# compacts the table: a reader may miss 'k', never sees another value.
d = {'z': 'Z', 'k': 'K'}


def churn_dict():
    del d['k']
    d['k'] = 'K'
    del d['z']
    d['z'] = 'Z'


def read_dict():
    bad = []
    for _ in range(100000):
        v = d.get('k')
        if v is not None and v != 'K':
            bad.append(v)
        try:
            if d['k'] != 'K':
                bad.append('sub')
        except KeyError:
            pass
        'k' in d
    return bad[:3]


print('dict', race(churn_dict, read_dict))

# A list that grows from four items to thousands and back.
L = [0] * 4


def churn_list():
    L.extend(range(2000))
    L.clear()
    L.append(0)


def read_list():
    return sum(1 for _ in range(10000) if -1 in L)


print('list', race(churn_list, read_list))


# Threads store and remove attributes of one object, and store distinct
# names on a fresh object at once, which makes its dict: none is lost.
class Plain:
    pass


shared = Plain()


def churn_attrs(k):
    for _ in range(100):
        for i in range(20):
            name = 'n%d_%d' % (k, i)
            setattr(shared, name, i)
            delattr(shared, name)


def fresh_attrs(o, bar, k):
    bar.wait()
    for i in range(8):
        setattr(o, 'a%d_%d' % (k, i), i)
        o.__dict__


ts = [threading.Thread(target=churn_attrs, args=(k,)) for k in range(4)]
for t in ts:
    t.start()
for t in ts:
    t.join()
lost = 0
for _ in range(50):
    o = Plain()
    bar = threading.Barrier(4)
    ts = [threading.Thread(target=fresh_attrs, args=(o, bar, k)) for k in range(4)]
    for t in ts:
        t.start()
    for t in ts:
        t.join()
    lost += 32 - len(o.__dict__)
print('attrs', len(shared.__dict__), lost)
