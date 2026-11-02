"""class bases invalidation."""
from support import loops


def run():
    class A: x = 1
    class B: x = 2
    class C(A): pass
    c = C()
    for _ in range(loops()): value = c.x
    C.__bases__ = (B,)
    return value, c.x


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
