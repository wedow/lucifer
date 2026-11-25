import operator
import threading


# int.__mul__ and range_iterator.__next__ called through their descriptors,
# hot enough to compile, then fed the operands at each boundary.
mul = int.__mul__
range_next = type(iter(range(0))).__next__


def products(pairs, rounds):
    out = None
    for _ in range(rounds):
        out = [mul(a, b) for a, b in pairs]
    return out


class Sub(int):
    pass


B30, B31, B32, B62, B63 = 1 << 30, 1 << 31, 1 << 32, 1 << 62, 1 << 63
small = [(3, -7), (0, 5), (-(B30 - 1), B30 - 1), (B30 - 1, B30 - 1), (-1, 1)]
print(products(small, 3000))
edges = [(B31, B31), (B31, B32), (-B31, B32), (B32, -B31), (B62, 2), (-B62, 2),
         (B62, -2), (-1, -B63), (-B63, -1), (-B63, 1), (B63 - 1, 1), (B63 - 1, -1),
         (B63, 0), (0, -B63), (1 << 100, -3), (-(B30), B30), (B30, B30), (B30 + 1, B32 - 1)]
for a, b in edges:
    print(a, b, mul(a, b) == a * b, mul(a, b))
print(products(edges, 3000) == [a * b for a, b in edges])
print(products(small, 3000))
odd = [(True, 5), (5, False), (True, True), (Sub(6), 7), (7, Sub(6)), (Sub(B62), Sub(4))]
for a, b in odd:
    r = mul(a, b)
    print(r, type(r).__name__)
print(products(odd, 3000) == [int(a) * int(b) for a, b in odd])
print(products(small, 3000))
for a, b in [(5, 'x'), (5, 2.0), (True, None)]:
    print(mul(a, b))
try:
    mul('x', 5)
except TypeError as exc:
    print(type(exc).__name__)


def drain(it, n):
    step = type(it).__next__
    got = []
    for _ in range(n):
        try:
            got.append(step(it))
        except StopIteration:
            got.append('stop')
    return got


for _ in range(3000):
    drain(iter(range(10)), 12)
cases = [range(0), range(5, 5), range(10, 0), range(0, 10, -1), range(10, 0, -3),
         range(-5, 5, 4), range(B63 - 5, B63 - 1), range(-B63 + 10, -B63 + 1, -3),
         range(B63 - 3, B63), range(-B63, -B63 + 2), range(B63 - 10, B63 - 1, 3),
         range(1 << 70, (1 << 70) + 5, 2), range(5, -(1 << 70), -(1 << 69))]
for r in cases:
    print(r, type(iter(r)).__name__, drain(iter(r), len(r) + 2))
for _ in range(3000):
    drain(iter(range(1 << 70, (1 << 70) + 3)), 4)
print(drain(iter(range(10, 0, -4)), 4))


def partial(it, k):
    for _ in range(k):
        range_next(it)
    return operator.length_hint(it), it.__length_hint__()


for _ in range(3000):
    partial(iter(range(100)), 7)
it = iter(range(100, 0, -7))
print(partial(it, 7), list(it), partial(it, 0), next(it, 'end'))
it = iter(range(3))
print(partial(it, 3), next(it, 'end'), drain(it, 2))

try:
    class R(range):
        pass
except TypeError as exc:
    print(type(exc).__name__)


def consume(it, n, out):
    total = 0
    for _ in range(n):
        try:
            total += range_next(it)
        except StopIteration:
            break
    out.append(total)


sums = []
threads = [threading.Thread(target=consume, args=(iter(range(i, 40000 + i)), 50000, sums)) for i in range(4)]
for t in threads:
    t.start()
for t in threads:
    t.join()
print(sorted(sums))
# Each thread steps the shared iterator until it is exhausted: CPython's
# step is unsynchronized, so its threads can lose decrements of the length
# and a fixed number of steps per thread need not exhaust it.
shared = iter(range(100000))
seen = []
threads = [threading.Thread(target=consume, args=(shared, 1 << 62, seen)) for _ in range(4)]
for t in threads:
    t.start()
for t in threads:
    t.join()
print(len(seen), next(shared, 'end'))
