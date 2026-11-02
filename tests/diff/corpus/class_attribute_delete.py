"""class attribute delete."""
from support import loops


def run():
    class A: x = 1
    class B(A): x = 2
    b = B()
    for _ in range(loops()): before = b.x
    del B.x
    return before, b.x


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
