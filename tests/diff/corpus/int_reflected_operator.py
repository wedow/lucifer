"""int reflected operator."""
from support import loops


def run():
    class R:
        def __radd__(self, other): return ('radd', other)
        def __rmul__(self, other): return ('rmul', other)
    class L:
        def __add__(self, other): return NotImplemented
    return 10 + R(), L() + R(), 4 * R()


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
