"""comprehension class scope."""
from support import loops


def run():
    x = 10
    class C:
        x = range(3)
        values = [i for i in x]
    return x, C.values


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
