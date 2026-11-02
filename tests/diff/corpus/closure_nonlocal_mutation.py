"""closure nonlocal mutation."""
from support import loops


def run():
    def outer():
        x = 0
        def add(n):
            nonlocal x
            x += n
            return x
        def read(): return x
        return add, read
    add, read = outer()
    return add(3), read(), add(2**90), read()


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
