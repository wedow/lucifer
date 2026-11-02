"""comprehension nested."""
from support import loops


def run():
    x = 'outer'
    values = [(i, j, x) for i in range(3) for j in range(i) if j % 2 == 0]
    return x, values


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
