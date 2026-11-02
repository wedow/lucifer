"""slots inheritance."""
from support import loops


def run():
    class A: __slots__ = ('x',)
    class B(A): __slots__ = ('y', '__dict__')
    b = B(); b.x = 1; b.y = 2; b.z = 3
    return b.x, b.y, vars(b)


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
