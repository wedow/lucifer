"""list slice alias."""
from support import loops


def run():
    a = [0, 1, 2, 3]; a[1:3] = a; before = a[:]; a[::2] = [9] * len(a[::2]); del a[::-3]
    return before, a


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
