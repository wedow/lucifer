# Loops that carry an object compiled code need not make: enumerate, zip,
# map, filter and reversed objects live only on the stack, and each step
# changes their fields (a count, the iterators they hold). A loop header
# carries those fields round the loop, the numbers among them as numbers,
# and a join carries what its edges changed differently. An exit must find
# the object as the loop left it (mid-loop, after a break, from an
# exception), an object the program takes out of the loop must be the one
# the loop stepped, and a count that outgrows a machine word must go on as
# an int. tools/jitcheck also runs this at two sizes and requires that the
# tuples, ints, lists and dicts compiled code allocates do not grow with
# the iterations.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


def enum_total(seq):
    s = 0
    for i, x in enumerate(seq):
        s += (i ^ x) & 15
    return s


def enum_from(seq, k):
    s = 0
    for i, x in enumerate(seq, k):
        s += i - x
    return s


def zip_total(a, b):
    s = 0
    for x, y in zip(a, b):
        s += x & y
    return s


def rev_total(seq):
    s = 0
    for x in reversed(seq):
        s = (s * 3 + x) % 1000003
    return s


def nested(rows):
    s = 0
    for i, row in enumerate(rows):
        for j, x in enumerate(row):
            s += (i + 1) * (j + 1) * x
    return s


def enum_zip(a, b):
    s = 0
    for i, (x, y) in enumerate(zip(a, b)):
        s += (i & 7) * (x - y)
    return s


def double(x):
    return x + x


def odd(x):
    return x & 1


def map_total(seq):
    s = 0
    for y in map(double, seq):
        s += y % 5
    return s


def filter_total(seq):
    s = 0
    for y in filter(odd, seq):
        s += y
    return s


def skips(seq):
    # The count is stepped on one path only where the paths join.
    s = 0
    it = enumerate(seq)
    for i, x in it:
        if x & 3 == 3:
            for j, y in it:
                s += j ^ y
                break
        s += i
    return s


def step(lst, tup, i):
    s = enum_total(lst) + enum_from(tup, i)
    s += zip_total(lst, tup)
    s += map_total(lst) + filter_total(tup)
    return s


L = [3, 1, 4, 1, 5, 9, 2, 6]
T = (2, 7, 1, 8, 2, 8)
ROWS = [[1, 2], [3], [4, 5, 6]]
step(L, T, 0)


def hot(n):
    s = 0
    for i in range(n):
        s += step(L, T, i)
    return s


print('hot', hot(N))

# The rest runs a fixed number of times, whatever N is.
K = 300


class Seq:
    def __init__(self, items):
        self.items = items

    def __len__(self):
        return len(self.items)

    def __getitem__(self, i):
        return self.items[i]


def rev_seq(seq):
    # reversed over the sequence protocol counts down in a field.
    s = 0
    for x in reversed(seq):
        s = s * 5 + x
    return s


def until(items, stop):
    # iter(callable, sentinel) drops its fields when it ends.
    s = 0
    it = iter(items).__next__
    for x in iter(it, stop):
        s += x
    return s


def others():
    out = []
    for r in range(K):
        out.append((rev_total(L), nested(ROWS), enum_zip(L, T), skips(T), skips(L), rev_seq(Seq(L)), until(L, 9),
                    until(T, 8 if r % 3 else 1)))
    return out[-1]


print('others', others())


def exits_mid_loop():
    """A guard fails inside the loop: the interpreter goes on with the
    enumerate where it stood."""
    out = []
    data = [1, 2, 3, 4, 5, 6]
    for r in range(K):
        s = 0
        for i, x in enumerate(data):
            s = s * 1000003 * 1000003 * 1000003 if r % 50 == 49 and x == 4 else s + i * x
        out.append(s % 1000033)
    return out[-5:]


print('exits_mid_loop', exits_mid_loop())


def taken_out():
    """The program keeps the object the loop stepped."""
    out = []
    data = (5, 6, 7, 8, 9)
    for r in range(K):
        it = enumerate(data)
        for i, x in it:
            if i == r % 4:
                break
        out.append(list(it))
    return out[-4:]


print('taken_out', taken_out())


def taken_out_zip():
    out = []
    a, b = [1, 2, 3, 4], (9, 8, 7, 6)
    for r in range(K):
        it = zip(a, b)
        for x, y in it:
            if x == 1 + r % 3:
                break
        out.append((next(it, None), it is iter(it)))
    return out[-3:]


print('taken_out_zip', taken_out_zip())


def big_counts():
    """The count leaves the machine word, and the digit, inside the loop."""
    out = []
    data = [1, 2, 3, 4]
    for r in range(K):
        start = (2 ** 62 if r % 7 == 3 else 2 ** 30 if r % 7 == 5 else 2 ** 63 - 2 if r % 7 == 6 else r) - 2
        s = 0
        for i, x in enumerate(data, start):
            s += i * x
        out.append(s)
    return out[-8:]


print('big_counts', big_counts())


def raises_mid_loop():
    out = []
    data = [3, 2, 1, 0, 5]
    for r in range(K):
        it = enumerate(data, r)
        try:
            for i, x in it:
                out.append(i // x)
        except ZeroDivisionError:
            out.append(next(it))
    return out[-6:]


print('raises_mid_loop', raises_mid_loop())


def mutates():
    """The list grows while an enumerate over it runs."""
    out = []
    for r in range(K):
        data = [1, 2, 3]
        seen = []
        for i, x in enumerate(data):
            seen.append((i, x))
            if x == 2 and r % 2:
                data.append(7)
        out.append(len(seen))
    return out[-4:]


print('mutates', mutates())


def strict():
    out = []
    for r in range(K):
        a = [1, 2, 3] if r % 9 else [1, 2]
        try:
            s = 0
            for x, y in zip(a, (4, 5, 6), strict=True):
                s += x * y
            out.append(s)
        except ValueError as e:
            out.append(str(e))
    return out[-10:]


print('strict', strict())


def mapped():
    out = []
    data = [1, 2, 3, 4, 5]
    for r in range(K):
        s = 0
        for y in map(double, data):
            s += y
        for y in filter(odd, data):
            s += y * r
        for y in filter(None, (0, r, 0, 2)):
            s -= y
        out.append(s)
    return out[-3:]


print('mapped', mapped())


def joins():
    """The enumerate is stepped on one arm of a branch before the paths
    join."""
    out = []
    data = (1, 2, 3, 4, 5, 6, 7)
    for r in range(K):
        it = enumerate(data, r)
        if r % 3:
            next(it)
        if r % 5 == 0:
            next(it)
            next(it)
        out.append(next(it))
    return out[-5:]


print('joins', joins())


def seen_by_frame():
    """A walker sees the enumerate mid-loop, and it is the one the loop
    goes on with."""
    out = []
    data = [1, 2, 3]
    for r in range(K):
        it = enumerate(data)
        for i, x in it:
            if r % 100 == 99 and i == 1:
                f = sys._getframe()
                out.append(f.f_locals['it'] is it)
                out.append(next(f.f_locals['it']))
    return out


print('seen_by_frame', seen_by_frame())


SEEN = []


def joined_tuple(a, flag):
    """Each arm builds its own tuple around a number both made an object
    of: after the join the item is the number of the arm that ran."""
    n = a + 1000
    if flag:
        SEEN.append(n)
        t = (n, 1)
        n = None
    else:
        SEEN.append(n)
        t = (n, 2)
        n = None
    return t[0] * 10 + t[1]


def joined_tuples():
    out = []
    for r in range(K):
        out.append(joined_tuple(r, r % 3 == 0))
        SEEN.clear()
    return out[-4:]


print('joined_tuples', joined_tuples())
