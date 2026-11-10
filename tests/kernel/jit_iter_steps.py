# FOR_ITER over iterators of other classes: the kernel's step for the
# class the loop met, in line. Loops switch classes, end early, raise in
# the middle and see their iterators mutated, so that compiled code leaves
# from inside the step as well.


class Count:
    def __init__(self, n):
        self.i = 0
        self.n = n

    def __iter__(self):
        return self

    def __next__(self):
        if self.i >= self.n:
            raise StopIteration
        self.i += 1
        return self.i


def total(it):
    s = 0
    for x in it:
        s += x if not isinstance(x, tuple) else x[0] + x[-1]
    return s


xs = list(range(50))
ys = [v * 0.5 for v in range(50)]
d = {i: i * i for i in range(30)}
r = []
for k in range(400):
    r = [total(zip(xs, ys)), total(enumerate(ys)), total(d.items()), total(d.values()),
         total(map(abs, xs)), total(filter(None, xs)), total(reversed(xs)), total(Count(9)),
         total(zip(xs, ys, xs)), total(iter('abc'.encode())), total({1, 2, 3})]
print(r)


def pairs(a, b):
    out = []
    for x, y in zip(a, b):
        out.append(x * y)
    return out


for k in range(400):
    p = pairs(xs, ys)
print(sum(p), pairs([1, 2], [3]), pairs('ab', [2, 3, 4]))
try:
    for k in range(300):
        pairs([1, 2], [3, 'x'])
except TypeError as e:
    print('TypeError:', e)


def strict(a, b):
    n = 0
    for x, y in zip(a, b, strict=True):
        n += 1
    return n


for k in range(300):
    v = strict(xs, ys)
print(v)
for a, b in [(xs, ys[:3]), (xs[:3], ys)]:
    try:
        strict(a, b)
    except ValueError as e:
        print('ValueError:', e)


def numbered(seq, start):
    return [(i, v) for i, v in enumerate(seq, start)]


for k in range(300):
    q = numbered('abc', k)
print(q, numbered([], 5))


def grow(d):
    n = 0
    for k, v in d.items():
        n += k
        if n > 100:
            d[n] = n          # changes size during iteration
    return n


try:
    for k in range(300):
        grow({i: i for i in range(10)})
    grow({i: i for i in range(30)})
except RuntimeError as e:
    print('RuntimeError:', e)


def gen_mid(n):
    def g():
        yield from range(n)
    return total(zip(g(), range(n)))


for k in range(300):
    m = gen_mid(20)
print(m)
