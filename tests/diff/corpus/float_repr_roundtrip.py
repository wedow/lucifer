"""float repr roundtrip."""
from support import loops


def run():
    values = [0.0, -0.0, 0.1, 1.2345678901234567, 1e-300, 1e300, float.fromhex('0x0.0000000000001p-1022')]
    return [(repr(x), float(repr(x)).hex(), x.hex()) for x in values]


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
