"""int multiply overflow."""
from support import loops


def run():
    def mul(a, b): return a * b
    return [(mul(x, x), mul(x, -x)) for x in (2, 2**31, 2**63-1, 2**130+3)]


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
