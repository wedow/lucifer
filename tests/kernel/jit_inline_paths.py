# List and tuple reads, list stores, int and float boxes, small list and
# tuple displays and [x] * n, as compiled code runs them in line: items the
# running thread owns, immortal items, items another thread made (whose
# reference is taken atomically), negative and out-of-range indexes, values
# whose last reference a store drops (their finalizers run), int boxes at
# the edges of the small-int cache and of one digit, and repeats of every
# size, including the ones that raise.
# Each result must be the interpreter's; tools/jitcheck also runs this at
# two sizes and requires that the C paths of these operations are not
# taken more often with more iterations (-X lucifer_jit_stats).
import sys
import threading

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class Box:
    def __init__(self, v):
        self.v = v


class Noisy:
    def __init__(self, name):
        self.name = name

    def __del__(self):
        print('del', self.name)


def reads(lst, tup, n):
    s = 0
    for i in range(n):
        s += lst[i % len(lst)].v + tup[i % len(tup)].v
    return s


def stores(lst, vals, n):
    for i in range(n):
        lst[i % len(lst)] = vals[i % len(vals)]
    return [b.v for b in lst]


def boxes(lst, n):
    t = 0.0
    for i in range(n):
        lst[i % len(lst)] = i * 7 - 100     # small and one-digit ints
        lst[(i + 1) % len(lst)] = i * 0.5   # floats
        t += lst[i % len(lst)]
    return t


def builds(x, n):
    total = 0
    for i in range(n):
        a = [x] * (i % 5)
        b = [x, i]
        c = (x, i)
        total += len(a) + len(b) + c[1] - b[1]
    return total


# The hot loops.
owned = [Box(k) for k in range(10)]
tup = tuple(Box(k) for k in range(7))
print('reads', reads(owned, tup, N))
print('stores', stores([Box(-1) for _ in range(5)], owned, N)[:5])
print('boxes', boxes([0] * 9, N))
print('builds', builds(owned[3], N))

# Immortal items, and items another thread made.
made = []
t = threading.Thread(target=lambda: made.extend(Box(k) for k in range(10)))
t.start()
t.join()


def count(lst, tup, n):
    hits = 0
    for i in range(n):
        if lst[i % len(lst)] is tup[i % len(tup)]:
            hits += 1
        lst[i % len(lst)] = tup[(i + 1) % len(tup)]
    return hits, lst


print('immortal', count([None, True, 5, (), ''], (None, False, 5, '', ()), 200))
print('foreign', reads(made, tuple(made), 200))
print('foreign stores', stores([None] * 4, made, 100))
print('mixed', count(made[:3] + [None], (made[0], None, made[2]), 100)[0])


def read_at(lst, i):
    return lst[i]


def tuple_at(t, i):
    return t[i]


def store_at(lst, i, v):
    lst[i] = v


for i in (0, 2, -1, -3, 3, -4, 100):
    for f, c in ((read_at, [1, 2, 3]), (tuple_at, (1, 2, 3))):
        try:
            print(f.__name__, i, f(c, i))
        except IndexError as e:
            print(f.__name__, i, 'IndexError', e)
    lst = [1, 2, 3]
    try:
        store_at(lst, i, 9)
        print('store', i, lst)
    except IndexError as e:
        print('store', i, 'IndexError', e)
try:
    read_at([], 0)
except IndexError as e:
    print('empty', e)

# A store that drops a value's last reference runs its finalizer at the store.
victims = [Noisy('a'), Noisy('b')]
for k in range(2):
    store_at(victims, k, k)
    print('after store', k)
print(victims)

# Lists that change size under a loop that reads them.
def shrink(lst):
    out = []
    for i in range(len(lst)):
        try:
            out.append(lst[i])
        except IndexError:
            out.append('gone')
        if i % 3 == 0 and lst:
            lst.pop()
    return out


print(shrink(list(range(12))))


def grow(lst, n):
    for i in range(n):
        lst.append(lst[i] + 1)
    return lst[-1], len(lst)


print(grow([0], 3000))

# Int boxes at the edges of the small-int cache and of one digit.
def add(a, b):
    return a + b


for v in (-6, -5, 0, 255, 256, 257, 2**30 - 2, 2**30 - 1, 2**30, -2**30, -2**30 + 1, 2**31, 2**62):
    r = add(v, 1)
    print('int', v + 1, r, type(r).__name__, r - 1 == v)
print('small identity', add(4, 1) is add(2, 3), add(255, 1) is add(128, 128))
print('float', add(0.5, 0.25), add(-0.0, -0.0), add(1e308, 1e308), add(float('nan'), 1.0) != add(1.0, 1.0))
floats = [add(k * 0.5, 0.25) for k in range(500)]  # more than the freelist holds
print('floats', sum(floats), len(set(map(id, floats))))


def rep(x, n):
    return x * n


def rrep(x, n):
    return n * x


class L(list):
    def __mul__(self, n):
        return 'L.__mul__ %d' % n


for n in (0, 1, 3, -2, True, False):
    print('rep', n, rep([1, 'a'], n), rep([], n), rrep([None], n))
print('rep sub', rep(L([1]), 3), type(rep([L()], 2)[0]).__name__)
a = [Box(1)]
r = rep(a, 4)
print('rep same', all(b is a[0] for b in r), len(r), r is not a)
for bad in (2.0, 'x', None):
    try:
        rep([1], bad)
    except TypeError as e:
        print('rep', type(bad).__name__, 'TypeError', e)
try:
    rep([0] * 65536, 2**29)
except MemoryError:
    print('rep MemoryError')
try:
    rep([0], 2**63)
except OverflowError as e:
    print('rep OverflowError', e)
