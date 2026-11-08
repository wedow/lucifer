# Loops over ranges, lists and tuples whose iterator compiled code need
# not make: the iterator lives only on the stack, so only an exit sees it,
# and each step makes the next one from the last one's fields. An exit
# must find the iterator where the loop stands (mid-loop, after a break,
# from an exception, in an inlined callee's loop), the list's own changes
# during the loop must show, and a tuple carried around a loop must stay
# one object wherever it is named. tools/jitcheck also runs this at two
# sizes and requires that the iterators and tuples compiled code allocates
# do not grow with the iterations.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class Vector:
    def __init__(self, items):
        self._storage = items
        self._first = 0
        self._last = len(items)

    def for_each(self, fn):
        for i in range(self._first, self._last):
            fn(self._storage[i])

    def has_some(self, fn):
        for i in range(self._first, self._last):
            if fn(self._storage[i]):
                return True
        return False


def total(seq):
    s = 0
    for x in seq:
        s += x
    return s


def first_over(seq, k):
    for x in seq:
        if x > k:
            return x
    return -1


def pairs(n):
    a, b = 0, 1
    t = (a, b)
    for i in range(n):
        a, b = t
        t = (b, (a + b) % 1009)
    return t[0] + t[1]


def note(x):
    return x


def big(x):
    return x > 3


def step(v, lst, tup, i):
    v.for_each(note)
    s = v.has_some(big)
    s += total(lst) + total(tup) + first_over(tup, 2)
    s += pairs(i % 7)
    for j in range(i % 3):
        s += j
    return s


V = Vector([1, 2, 3, 4, 5])
L = [1, 2, 3]
T = (1, 2, 3, 4)
step(V, L, T, 0)


def hot(n):
    s = 0
    for i in range(n):
        s += step(V, L, T, i)
    return s


print('hot', hot(N))

# The rest runs a fixed number of times, whatever N is.
K = 300


def exits_mid_loop():
    """A guard fails inside the loop: the interpreter goes on from there."""
    out = []
    for r in range(K):
        big = 1
        s = 0
        for i in range(10):
            big = big * 1000003 if r % 50 == 49 and i == 5 else big
            s += i * big
        out.append(s % 1000033)
    return out[-5:]


print('exits_mid_loop', exits_mid_loop())


def exits_in_list_loop():
    out = []
    data = [1, 2, 3, 4, 5, 6]
    for r in range(K):
        s = 0
        for x in data:
            s = s * 1000003 if r % 60 == 59 and x == 4 else s + x
        out.append(s % 1000033)
    return out[-3:]


print('exits_in_list_loop', exits_in_list_loop())


def mutates():
    """The list grows and shrinks while it is iterated."""
    out = []
    for r in range(K):
        data = [1, 2, 3]
        seen = []
        for x in data:
            seen.append(x)
            if x == 2 and r % 2:
                data.append(9)
            if x == 1 and r % 3 == 0:
                data.pop()
        out.append(tuple(seen))
    return sorted(set(out))


print('mutates', mutates())


def breaks():
    n = 0
    for r in range(K):
        for x in (5, 6, 7, 8):
            if x == 7:
                break
            n += x
        else:
            n -= 1000
        for i in range(r, r + 5):
            if i % 4 == 0:
                break
        n += i
    return n


print('breaks', breaks())


def raises():
    """An exception inside the loop: the handler sees where it stood."""
    out = []
    for r in range(K):
        try:
            for i in range(r % 7, 10):
                if i == 8 and r % 40 == 39:
                    raise ValueError(i)
        except ValueError as e:
            out.append((r, e.args[0], i))
    return out


print('raises', raises())


def walker(depth):
    f = sys._getframe(depth)
    return sorted(k for k in f.f_locals if not k.startswith('_'))


def walked():
    """A frame walker reads the locals while the iterator is virtual."""
    out = []
    for r in range(K):
        for i in range(3):
            if r % 100 == 99 and i == 1:
                out.append(walker(1))
    return out


print('walked', walked())


def explicit_iter():
    """An iterator a local names is one object: not made per step."""
    out = []
    for r in range(K):
        it = iter([1, 2, 3, 4])
        first = next(it)
        rest = [x for x in it]
        it2 = iter(range(4))
        for x in it2:
            if x == 1:
                break
        out.append((first, rest, next(it2), list(it2)))
    return out[-1]


print('explicit_iter', explicit_iter())


def carried_tuple():
    """A tuple carried around a loop: one object for every name, at exits too."""
    out = []
    t = (0, 1)
    for r in range(K):
        u = t
        a, b = t
        t = (b, a + b if r % 90 != 89 else (a + b) * 10 ** 20)
        if r % 90 == 89:
            out.append((u is not t, t[1] % 997, type(t).__name__, len(u)))
    return out


print('carried_tuple', carried_tuple())


def nested():
    s = 0
    for r in range(K):
        for i in range(3):
            for j in (1, 2):
                for k in [i, j]:
                    s += k * (r % 5)
    return s


print('nested', nested())


def negative_and_empty():
    s = 0
    for r in range(K):
        for i in range(-r, -r + 3):
            s += i
        for i in range(5, 2):
            s += 1000
        for x in ():
            s += 1000
        for x in []:
            s += 1000
    return s


print('negative_and_empty', negative_and_empty())


def gen_body():
    """Loops inside a generator keep their iterators real across yields."""
    def g(n):
        for i in range(n):
            for x in (i, i + 1):
                yield x
    return [sum(g(r % 6)) for r in range(K)][-6:]


print('gen_body', gen_body())
