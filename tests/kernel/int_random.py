# Pseudo-random ints of many sizes through every digit algorithm, with
# identities checked and a checksum of the results printed.
state = 123456789
def rand_bits(bits):
    global state
    r = 0
    n = 0
    while n < bits:
        state = (state * 6364136223846793005 + 1442695040888963407) % (2 ** 64)
        r = (r << 32) | (state >> 32)
        n += 32
    return r >> (n - bits)

def pick(k):
    v = (k * 2654435761) % 10007
    return 1 + v * v // 11000

check = 0
bad = 0
i = 0
while i < 160:
    a = rand_bits(pick(i)) - rand_bits(8) * (i % 3 - 1) * 2 ** (pick(i) // 2)
    b = rand_bits(pick(i * 7 + 3)) + 1
    if i % 5 == 0:
        b = -b
    q, r = a.__divmod__(b)
    if q * b + r != a:
        bad += 1
    if (r != 0 and (r < 0) != (b < 0)) or (r if b > 0 else -r) >= (b if b > 0 else -b):
        bad += 1
    if a * b != b * a or (a + b) - b != a or (a - b) + b != a:
        bad += 1
    if (a * b) // b != a or (a * b) % b != 0:
        bad += 1
    if (a << 77) >> 77 != a or (a >> 13) << 13 > a:
        bad += 1
    if (a & b) + (a | b) != a + b or (a ^ b) != (a | b) - (a & b):
        bad += 1
    if ~a != -a - 1 or -(-a) != a:
        bad += 1
    if int(a.__repr__()) != a or int(a.__repr__(), 10) != a:
        bad += 1
    if int.from_bytes(a.to_bytes(1300, 'little', signed=True), 'little', signed=True) != a:
        bad += 1
    sq = a * a
    if sq < 0 or (a != 0 and sq // a != a):
        bad += 1
    check = (check * 31 + q + r + (a & b) + (a | b) + (a ^ b) + (a >> 7) + (a << 5)) % (2 ** 89 - 1)
    check = (check + a.__pow__(65537, 2 ** 127 - 1) + a * b) % (2 ** 89 - 1)
    i += 1
print(bad, check)
x = rand_bits(20000)
y = rand_bits(7000)
z = rand_bits(70)
print((x * y) % 1000000007, (x * y) // y == x, (x * x) % 998244353, (x * z) // z == x)
print(x.bit_length(), y.bit_length(), (x * y).bit_length(), x.bit_count() + y.bit_count())
qq, rr = x.__divmod__(z)
print(x % y, x // y == (x - x % y) // y, rr)
print((x ** 3).bit_length(), (z ** 40).__repr__())
