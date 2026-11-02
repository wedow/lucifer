"""float hex ratio."""
from support import loops


def run():
    return [(float.fromhex(s).hex(), float.fromhex(s).as_integer_ratio()) for s in ('0x1.8p+1', '-0x0p+0', '0x1p-1074', '0x1.fffffffffffffp+1023')]


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
