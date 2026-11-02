"""closure cell mutation."""
from support import loops


def run():
    def outer():
        x = 4
        return lambda: x
    f = outer()
    for _ in range(loops()): before = f()
    f.__closure__[0].cell_contents = 'changed'
    return before, f()


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
