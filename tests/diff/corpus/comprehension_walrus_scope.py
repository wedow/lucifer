"""comprehension walrus scope."""
from support import loops


def run():
    y = -1
    values = [y := x * 2 for x in range(4)]
    return y, values


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
