# int and bool arithmetic on compact and multi-digit values. Only operators,
# dunder calls and print: the builtins that wrap them arrive later.
a = 17
b = 5
print(a + b, a - b, a * b, a // b, a % b)
print(-a + b, -a - b, -a * b, -a // b, -a % b)
print(a + -b, a - -b, a * -b, a // -b, a % -b)
print(-a + -b, -a - -b, -a * -b, -a // -b, -a % -b)
q, r = a.__divmod__(b)
print(q, r)
q, r = (-a).__divmod__(b)
print(q, r)
q, r = a.__divmod__(-b)
print(q, r)
q, r = (-a).__divmod__(-b)
print(q, r)
print(0 // 7, 0 % 7, 0 // -7, 0 % -7)
print(a ** 0, a ** 1, a ** 2, 2 ** 10, (-2) ** 3, (-2) ** 4, 0 ** 0)
print(a.__pow__(3, 7), a.__pow__(100, 13), (-a).__pow__(3, 7), a.__pow__(3, -7), a.__pow__(3, 1))
print(a.__pow__(-1, 19), a.__pow__(-3, 19))
print(a << 3, a >> 2, -a << 3, -a >> 2, -1 >> 100, 1 >> 100, 0 << 100)
print(a & b, a | b, a ^ b, -a & b, -a | b, -a ^ b, -a & -b, -a | -b, -a ^ -b)
print(~a, ~-a, ~0, -(-a), +a)
print(a.__abs__(), (-a).__abs__(), 0 .__abs__())
print(a < b, a <= b, a == b, a != b, a > b, a >= b)
print(a < a, a <= a, a == a, a != a, a > a, a >= a)
print(-a < b, -a > -b, -a == -a)
print(a.bit_length(), (-a).bit_length(), 0 .bit_length(), 255 .bit_length(), 256 .bit_length())
print(a.bit_count(), (-a).bit_count(), 0 .bit_count(), 255 .bit_count())
print(a.conjugate(), a.real, a.imag, a.numerator, a.denominator)
print(a.__index__(), a.__int__(), a.__trunc__(), a.__floor__(), a.__ceil__())
print(a.__round__(), a.__round__(0), 1234 .__round__(-2), 1250 .__round__(-2), 1350 .__round__(-2), -1250 .__round__(-2))
print(a.is_integer())
n, d = a.as_integer_ratio()
print(n, d)
print(a.__hash__(), (-a).__hash__(), (-1).__hash__(), (2 ** 61).__hash__(), (2 ** 61 - 1).__hash__())
print(a.__bool__(), 0 .__bool__())
print(a.__add__(b), a.__radd__(b), a.__sub__(b), a.__rsub__(b), a.__rfloordiv__(b), a.__rmod__(b))
print(a.__add__('x') is NotImplemented, a.__lt__('x') is NotImplemented, a.__eq__('x') is NotImplemented, a.__ne__('x') is NotImplemented, a.__radd__(1.5) is NotImplemented)
print(a.__str__(), a.__repr__(), (-a).__repr__())
x = 5
x += 3
x -= 1
x *= 6
x //= 4
x %= 7
x **= 3
x <<= 2
x >>= 1
x &= 0xffff
x |= 0x100
x ^= 0x0f
print(x)
total = 0
i = 0
while i < 100:
    total += i * i - i // 3
    i += 1
print(total)
print(1 if a else 2, 1 if 0 else 2, not a, not 0)
print(a is a)
print(-5 // 2, -5 % 2, 5 // -2, 5 % -2, -5 // -2, -5 % -2)
print(1_000_000, 0x1f, 0o17, 0b101)
print(2 ** 29, 2 ** 30, 2 ** 31, 2 ** 62, 2 ** 63, 2 ** 64, -2 ** 63)
print(2 ** 30 - 1 + 1, 2 ** 30 + 2 ** 30, (2 ** 30) * (2 ** 30), (2 ** 60) // (2 ** 30))
print(9223372036854775807 + 1, -9223372036854775808 - 1, 9223372036854775807 * 2)
print(2 ** 63 // 3, 2 ** 63 % 1000, -2 ** 63 // 3, -2 ** 63 % 1000)
