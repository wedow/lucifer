# &, |, ^ and the shifts on ints around every digit and machine-word
# boundary, both signs, with the results printed as a checksum and in part.
edges = [0, 1, 2, 3, 5, 2 ** 29, 2 ** 30 - 1, 2 ** 30, 2 ** 30 + 1, 2 ** 31, 2 ** 32 - 1, 2 ** 32,
         2 ** 59, 2 ** 60 - 1, 2 ** 60, 2 ** 60 + 1, 2 ** 61, 2 ** 62 - 1, 2 ** 62, 2 ** 63 - 1, 2 ** 63,
         2 ** 63 + 1, 2 ** 64 - 1, 2 ** 64, 2 ** 89, 2 ** 90 - 1, 2 ** 90, 2 ** 120 + 12345,
         0xdeadbeefcafe, 0x123456789abcdef0123456789, (1 << 300) - 1, 7 ** 200]
values = edges + [-v for v in edges if v]
counts = [0, 1, 2, 5, 29, 30, 31, 32, 59, 60, 61, 62, 63, 64, 65, 89, 90, 91, 200]

check = 0
for a in values:
    for b in values:
        check = (check * 1000003 + (a & b) * 3 + (a | b) * 5 + (a ^ b) * 7) % (2 ** 127 - 1)
print('bitwise', check)
print([a & -a for a in values[:12]], [hex(a ^ (a >> 1)) for a in edges[8:14]])
print(0xffffffffff & 0x3ff, (2 ** 40 - 7) >> 30, ((2 ** 40 - 7) >> 20) & 0x3ff, -(2 ** 40) | 1023)

check = 0
for a in values:
    for n in counts:
        left, right = a << n, a >> n
        if (left >> n) != a or right != a // 2 ** n:
            print('bad shift', a, n)
        check = (check * 1000003 + left + right * 11) % (2 ** 127 - 1)
print('shift', check)
print([(-v) >> 1 for v in edges[:10]], [v << 3 for v in edges[5:9]], -1 >> 1000, 5 >> 1000, -5 >> 10 ** 30)

for f in (lambda: 1 << -1, lambda: 1 >> -1, lambda: 1 << 2 ** 100, lambda: 1 << 2 ** 62,
          lambda: (2 ** 70) >> -(2 ** 70), lambda: 3 & 1.0, lambda: 1.0 | 2, lambda: 'a' ^ 1):
    try:
        print(f())
    except (ValueError, OverflowError, TypeError, MemoryError) as e:
        print(type(e).__name__, e)
print(0 << 2 ** 100, True & 3, True | False, True ^ True, type(True & True).__name__, type(True & 1).__name__)
x = 2 ** 64 + 5
y = x
x &= 2 ** 40 - 1
x |= 1 << 70
x ^= 3
x <<= 2
x >>= 1
print(x, y)
