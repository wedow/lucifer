# random.getrandbits and randbytes: CPython's words, least significant first,
# for every width class, and the argument errors.
import random

r = random.Random(12345)
for k in (0, 1, 2, 31, 32, 33, 63, 64, 65, 95, 96, 97, 1000, 4096):
    print(k, r.getrandbits(k))
big = r.getrandbits(1_000_003)
print(big.bit_length() <= 1_000_003, hash(big), big % 1_000_000_007)
data = r.randbytes(100_001)
print(len(data), data[:16].hex(), data[-16:].hex(), hash(data[::997]) == hash(bytes(data[::997])))
print(sum(data) % 65521, r.random(), r.getrandbits(True))


class Sub(random.Random):
    pass


print(Sub(7).getrandbits(100), random.Random(7).getrandbits(100))
for k in (-1, -2 ** 70, 2 ** 64, 1.5, '3', None):
    try:
        r.getrandbits(k)
    except (ValueError, OverflowError, TypeError) as e:
        print(type(e).__name__, e)
for n in (-1, 0, 1, 3):
    try:
        print(r.randbytes(n))
    except ValueError as e:
        print('ValueError', e)
