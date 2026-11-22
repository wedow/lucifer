import sys


# Exercise the kernel primitives through their descriptors. The multiplication
# and iteration bytecodes already have their own lowering.
range_next = type(iter(range(0))).__next__


def dot(it, n):
    total = 0
    i = 0
    while i < n:
        value = range_next(it)
        total += int.__mul__(value, i & 7)
        i += 1
    return total


def product(a, b):
    return int.__mul__(a, b)


def mixed_products(n):
    matches = 0
    for i in range(n):
        wide = i % 8 == 0
        a = (1 << 62) if wide else i
        expected = (1 << 64) if wide else i << 2
        matches += product(a, 4) == expected
    return matches


class Int(int):
    def __mul__(self, other):
        return 'override'


n = int(sys.argv[1]) if len(sys.argv) > 1 else 1000
dot(iter(range(100)), 100)
it = iter(range(5, 5 - 2 * n, -2))
print(dot(it, n), next(it, 'end'), next(it, 'still exhausted'))

edge = iter(range(1 << 62, (1 << 62) + 4))
print(dot(edge, 3), next(edge))

for a, b in [(3, -7), (-(1 << 62), 2), (1 << 62, 4), (1 << 100, -3),
             (Int(9), True), (5, 'x')]:
    result = product(a, b)
    print(result, type(result).__name__)

wide = iter(range(1 << 70, (1 << 70) + 5, 2))
print([next(wide, None) for _ in range(5)])

try:
    dot(it, 1)
except StopIteration as exc:
    print(type(exc).__name__, exc.value)

print(mixed_products(1000))
