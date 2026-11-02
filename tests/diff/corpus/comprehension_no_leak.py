"""comprehension no leak."""
from support import loops


def run():
    x = 99; values = [x * x for x in range(4)]
    d = {x: x + 1 for x in range(3)}; s = {x for x in range(2)}
    return x, values, list(d.items()), sorted(s)


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
