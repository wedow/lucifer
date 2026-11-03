# Multi-digit ints: all algorithms, conversions and the str limit.
def fact(n):
    r = 1
    i = 2
    while i <= n:
        r *= i
        i += 1
    return r

f = fact(60)
print(f)
print(-f)
print(f // fact(58), f % 1000003, f // 10 ** 40, f % 10 ** 40)
print((-f) // 10 ** 40, (-f) % 10 ** 40, f // -(10 ** 40), f % -(10 ** 40))
q, r = f.__divmod__(fact(30) + 12345)
print(q, r)
q, r = (-f).__divmod__(fact(30) + 12345)
print(q, r)
print(f * f, f * f == f ** 2, (f * f) // f == f)
print(f + f, f - f, f - (f + 1), (f + 1) - f, -f - f)
g = 3 ** 500
h = 7 ** 400
print(g * h)
print((g * h) // g == h, (g * h) % g)
print(g // h, g % h)
print(h // g, h % g)
big = 12345678901234567890 ** 30
print(big)
print(big * big // big == big)
print(big.bit_length(), big.bit_count(), (-big).bit_length())
print(big >> 100, big << 100, -big >> 100, -big << 100)
print(big >> 100000, -big >> 100000, big << 0, big >> 0)
print(big & f, big | f, big ^ f, -big & f, -big | f, -big ^ f, big & -f, big | -f, big ^ -f, -big & -f, -big | -f, -big ^ -f)
print(f.__pow__(12345, 10 ** 30 + 57))
print(g.__pow__(h, 2 ** 89 - 1))
print(g.__pow__(-1, 2 ** 89 - 1), (2 ** 89 - 1).__pow__(5, -(10 ** 20)))
print(2 ** 200, 3 ** 200, 10 ** 100, (-3) ** 99)
print(big.__hash__(), (-big).__hash__(), f.__hash__(), (10 ** 100).__hash__())
print(big < big + 1, big <= big, big == big, big != big + 1, big > big - 1, big >= big + 1)
print(big == 5, big < 5, big > 5, -big < 5, -big > 5, 5 < big)
print(2 ** 100 == 2 ** 100, (2 ** 100).__eq__(2 ** 100 + 0))
a = 10 ** 25 + 7
b = 10 ** 12 + 3
x = 0
i = 0
while i < 40:
    x = (x * a + b) % (2 ** 127 - 1)
    i += 1
print(x)
# Karatsuba and lopsided multiplication
p = 7 ** 3000
q = 11 ** 2000
print((p * q) % 1000000007, (p * q) // q == p, (p * p) % 998244353, (p * 3) % 1000)
print((p * q).bit_length(), (p * p).bit_length())
# division with a long normalised divisor
d = 2 ** 300 - 2 ** 150 + 1
n = 3 ** 900
q, r = n.__divmod__(d)
print(q * d + r == n, r < d, r >= 0)
# string limit round trips below the default of 4300 digits
s = (10 ** 4000).__str__()
print(((10 ** 4000) + 1).__str__() == s)
# bytes round trips
v = 0x123456789abcdef0123456789abcdef
print(int.from_bytes(v.to_bytes(16, 'big'), 'big') == v)
print(int.from_bytes(v.to_bytes(20, 'little'), 'little') == v)
print(int.from_bytes((-v).to_bytes(17, 'big', signed=True), 'big', signed=True) == -v)
print(int.from_bytes(b'\xff\xff', 'big'), int.from_bytes(b'\xff\xff', 'little', signed=True), int.from_bytes(b'', 'big'))
print(int('123456789012345678901234567890'), int('-123456789012345678901234567890'), int('  +42  '))
print(int('1_000_000'), int('ff', 16), int('FF', 16), int('0xff', 16), int('0xff', 0), int('0b101', 0), int('0o17', 0), int('z', 36), int('-0x10', 0))
print(int('1' * 100, 2), int('7' * 50, 8), int('f' * 40, 16), int('z' * 30, 36), int('9' * 80))
print(int(b'42'), int(b' -7 '), int('0'), int('-0'), int('00'), int('0_0'))
print(int(True), int(7), int(-7))
print(int())
