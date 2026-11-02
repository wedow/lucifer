"""list iteration mutation."""
from support import loops


def run():
    values = [1, 2, 3]; seen = []
    for x in values:
        seen.append(x)
        if x == 1: values.append(4)
    return seen, values


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
