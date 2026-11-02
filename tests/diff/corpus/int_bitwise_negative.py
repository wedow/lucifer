"""int bitwise negative."""
from support import loops


def run():
    return [(~x, x & 255, x | 255, x ^ 255, x.bit_count(), x.bit_length()) for x in (-2**100-1, -1, 0, 2**100+1)]


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
