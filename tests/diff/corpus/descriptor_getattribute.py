"""descriptor getattribute."""
from support import loops


def run():
    class C:
        def __init__(self): object.__setattr__(self, 'x', 3)
        def __getattribute__(self, name):
            if name == 'x': return object.__getattribute__(self, name) * 7
            return object.__getattribute__(self, name)
    c = C(); before = c.x; c.x = 9
    return before, c.x, vars(c)


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
