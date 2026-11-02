"""int add overflow."""
from support import loops


def run():
    def add(a, b): return a + b
    values = [0, 1, -1, 2**30-1, 2**63-1, -(2**63), 2**200]
    return [(a, add(a, 1), add(a, -1)) for a in values]


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
