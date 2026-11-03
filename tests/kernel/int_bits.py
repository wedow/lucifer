# Bit-level and boundary behaviour of ints: digit boundaries, shifts,
# two's complement operations, bytes conversion and bases.
vals = (0, 1, -1, 2, -2, 29, 30, 31, 2 ** 30 - 1, 2 ** 30, 2 ** 30 + 1, -(2 ** 30), -(2 ** 30) - 1,
        2 ** 60 - 1, 2 ** 60, -(2 ** 60), 2 ** 63, -(2 ** 63), 2 ** 64 - 1, 2 ** 90, -(2 ** 90) + 1,
        3 ** 77, -(3 ** 77), 10 ** 40 + 12345)
for a in vals:
    for b in vals:
        print(a, b, a + b, a - b, a * b, a & b, a | b, a ^ b)
        if b != 0:
            print(a // b, a % b)
for a in vals:
    print(a, a.bit_length(), a.bit_count(), ~a, -a, a << 1, a >> 1, a << 31, a >> 31, a << 60, a >> 61)
    print(a.__hash__(), a.__abs__(), a.__index__(), a < 0, a == 0, a.__bool__())
for a in vals:
    for n in (0, 1, 9, 31, 90, 200):
        print(a, n, a << n, a >> n)
for a in vals:
    for e in (0, 1, 2, 3, 5, 10):
        print(a, e, a ** e)
for a in vals:
    for m in (1, 2, 7, -7, 2 ** 31 - 1, 10 ** 20 + 39, -(10 ** 20) - 39):
        print(a, m, a.__pow__(5, m), a.__pow__(0, m), a.__pow__(2 ** 70 + 3, m))
for a in vals:
    for n in (1, 2, 3, 4, 8, 12, 16):
        if a.bit_length() < 8 * n - 1:
            print(a, n, int.from_bytes(a.to_bytes(n, 'big', signed=True), 'big', signed=True),
                  int.from_bytes(a.to_bytes(n, 'little', signed=True), 'little', signed=True))
        if a >= 0 and a.bit_length() <= 8 * n:
            print(a, n, int.from_bytes(a.to_bytes(n, 'big'), 'big'), int.from_bytes(a.to_bytes(n, 'little'), 'little'))
print(int.from_bytes(b'\x80', 'big', signed=True), int.from_bytes(b'\x80', 'big'), int.from_bytes(b'\x00\x80', 'big', signed=True))
print(int.from_bytes(b'\xff' * 20, 'little', signed=True), int.from_bytes(b'\xff' * 20, 'big'))
print(int.from_bytes(b'', 'little', signed=True), int.from_bytes(b'\x00\x00\x01', 'big'))
for base in (2, 3, 7, 8, 10, 16, 30, 36):
    print(int('1111111111111111111111111111111111111111', base), int('-10101010101010', base))
print(int('0b1_0_1', 0), int('0x_ff', 0), int('0_7', 8), int('1_000', 10))
print(int('99999999999999999999999999999999999999999999999999999999999999999'), int('-99999999999999999999999999999999999999999999999999999999999999999'), int('10000000000000000000000000000000000000000000000000000000000000000') - 1 == int('9999999999999999999999999999999999999999999999999999999999999999'))
