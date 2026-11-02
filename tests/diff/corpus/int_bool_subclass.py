"""int bool subclass."""
from support import loops


def run():
    class N(int):
        def __add__(self, other): return ('override', int(self), other)
    return True + True, isinstance(True, int), N(4) + 2, 2 + N(4), type(N(4) * 3).__name__


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
